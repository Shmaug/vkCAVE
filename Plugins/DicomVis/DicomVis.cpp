#include <Scene/MeshRenderer.hpp>
#include <Content/Font.hpp>
#include <Scene/GUI.hpp>
#include <Util/Profiler.hpp>

#include <Core/EnginePlugin.hpp>
#include <assimp/pbrmaterial.h>

#include "Dicom.hpp"

using namespace std;

class DicomVis : public EnginePlugin {
private:
	Scene* mScene;
	vector<Object*> mObjects;
	Object* mSelected;

	uint32_t mFrameIndex;

	float3 mVolumePosition;
	quaternion mVolumeRotation;
	float3 mVolumeScale;

	Texture* mRawVolume;
	bool mRawVolumeNew;
	
	struct FrameData {
		Texture* mBakedVolume;
		Texture* mOpticalDensity;
		bool mImagesNew;
	};
	FrameData* mFrameData;

	Camera* mMainCamera;

	MouseKeyboardInput* mInput;

	float mZoom;


	bool mShowPerformance;
	bool mSnapshotPerformance;
	ProfilerSample mProfilerFrames[PROFILER_FRAME_COUNT - 1];
	uint32_t mSelectedFrame;

	float mFrameTimeAccum;
	float mFps;
	uint32_t mFrameCount;

	std::set<fs::path> mDicomFolders;

public:
	PLUGIN_EXPORT DicomVis(): mScene(nullptr), mSelected(nullptr), mShowPerformance(false), mSnapshotPerformance(false),
		mFrameCount(0), mFrameTimeAccum(0), mFps(0), mFrameIndex(0), mRawVolume(nullptr), mRawVolumeNew(false) {
		mEnabled = true;
	}
	PLUGIN_EXPORT ~DicomVis() {
		safe_delete(mRawVolume);
		for (uint32_t i = 0; i < mScene->Instance()->Device()->MaxFramesInFlight(); i++) {
			safe_delete(mFrameData[i].mBakedVolume);
			safe_delete(mFrameData[i].mOpticalDensity);
		}
		for (Object* obj : mObjects)
			mScene->RemoveObject(obj);
	}

	PLUGIN_EXPORT bool Init(Scene* scene) override {
		mScene = scene;
		mInput = mScene->InputManager()->GetFirst<MouseKeyboardInput>();

		mZoom = 3.f;

		shared_ptr<Camera> camera = make_shared<Camera>("Camera", mScene->Instance()->Window());
		mScene->AddObject(camera);
		camera->Near(.01f);
		camera->Far(800.f);
		camera->FieldOfView(radians(65.f));
		camera->LocalPosition(0, 1.6f, -mZoom);
		mMainCamera = camera.get();
		mObjects.push_back(mMainCamera);

		#pragma region plane
		auto planeMat = make_shared<Material>("Plane", mScene->AssetManager()->LoadShader("Shaders/pbr.stm"));
		planeMat->EnableKeyword("TEXTURED");
		planeMat->SetParameter("MainTextures", 0, mScene->AssetManager()->LoadTexture("Assets/Textures/grid.png"));
		planeMat->SetParameter("NormalTextures", 0, mScene->AssetManager()->LoadTexture("Assets/Textures/bump.png"));
		planeMat->SetParameter("MaskTextures", 0, mScene->AssetManager()->LoadTexture("Assets/Textures/mask.png"));
		planeMat->SetParameter("TextureST", float4(256, 256, 1, 1));
		planeMat->SetParameter("Color", float4(1));
		planeMat->SetParameter("Metallic", 0.f);
		planeMat->SetParameter("Roughness", .5f);
		planeMat->SetParameter("BumpStrength", 1.f);
		planeMat->SetParameter("Emission", float3(0));

		auto plane = make_shared<MeshRenderer>("Plane");
		plane->Mesh(shared_ptr<Mesh>(Mesh::CreatePlane("Plane", mScene->Instance()->Device(), 512.f)));
		plane->Material(planeMat);
		plane->PushConstant("TextureIndex", 0u);
		plane->LocalRotation(quaternion(float3(-PI / 2, 0, 0)));
		mScene->AddObject(plane);
		mObjects.push_back(plane.get());
		#pragma endregion

		mScene->Environment()->EnableCelestials(false);
		mScene->Environment()->EnableScattering(false);
		mScene->Environment()->AmbientLight(.6f);

		for (const auto& p : fs::recursive_directory_iterator("E:/Data/larry colon"))
			if (p.path().extension().string() == ".dcm")
				mDicomFolders.emplace(p.path().parent_path());

		mFrameData = new FrameData[mScene->Instance()->Device()->MaxFramesInFlight()];
		memset(mFrameData, 0, sizeof(FrameData) * mScene->Instance()->Device()->MaxFramesInFlight());

		return true;
	}
	PLUGIN_EXPORT void Update() override {
		if (mInput->KeyDownFirst(KEY_F1))
			mScene->DrawGizmos(!mScene->DrawGizmos());
		if (mInput->KeyDownFirst(KEY_TILDE))
			mShowPerformance = !mShowPerformance;

		// Snapshot profiler frames
		if (mInput->KeyDownFirst(KEY_F3)) {
			mSnapshotPerformance = !mSnapshotPerformance;
			if (mSnapshotPerformance) {
				mSelectedFrame = PROFILER_FRAME_COUNT;
				queue<pair<ProfilerSample*, const ProfilerSample*>> samples;
				for (uint32_t i = 0; i < PROFILER_FRAME_COUNT - 1; i++) {
					mProfilerFrames[i].mParent = nullptr;
					samples.push(make_pair(mProfilerFrames + i, Profiler::Frames() + ((i + Profiler::CurrentFrameIndex() + 2) % PROFILER_FRAME_COUNT)));
					while (samples.size()) {
						auto p = samples.front();
						samples.pop();

						p.first->mStartTime = p.second->mStartTime;
						p.first->mDuration = p.second->mDuration;
						strncpy(p.first->mLabel, p.second->mLabel, PROFILER_LABEL_SIZE);
						p.first->mChildren.resize(p.second->mChildren.size());

						auto it2 = p.second->mChildren.begin();
						for (auto it = p.first->mChildren.begin(); it != p.first->mChildren.end(); it++, it2++) {
							it->mParent = p.first;
							samples.push(make_pair(&*it, &*it2));
						}
					}
				}
			}
		}

		mZoom = clamp(mZoom - mInput->ScrollDelta() * .2f, -1.f, 5.f);
		mMainCamera->LocalPosition(0, 1.6f, -mZoom);

		if (mInput->KeyDown(MOUSE_LEFT)) {
			float3 axis = mMainCamera->WorldRotation() * float3(0, 1, 0) * mInput->CursorDelta().x + mMainCamera->WorldRotation() * float3(1, 0, 0) * mInput->CursorDelta().y;
			if (dot(axis, axis) > .001f)
				mVolumeRotation = quaternion(length(axis) * .003f, -normalize(axis)) * mVolumeRotation;
		}

		// count fps
		mFrameTimeAccum += mScene->Instance()->DeltaTime();
		mFrameCount++;
		if (mFrameTimeAccum > 1.f) {
			mFps = mFrameCount / mFrameTimeAccum;
			mFrameTimeAccum -= 1.f;
			mFrameCount = 0;
		}
	}

	PLUGIN_EXPORT void PreRenderScene(CommandBuffer* commandBuffer, Camera* camera, PassType pass) override {
		if (pass != PASS_MAIN || camera != mScene->Cameras()[0]) return;
		Font* reg14 = mScene->AssetManager()->LoadFont("Assets/Fonts/OpenSans-Regular.ttf", 14);
		Font* sem11 = mScene->AssetManager()->LoadFont("Assets/Fonts/OpenSans-SemiBold.ttf", 11);
		Font* sem16 = mScene->AssetManager()->LoadFont("Assets/Fonts/OpenSans-SemiBold.ttf", 16);
		Font* bld24 = mScene->AssetManager()->LoadFont("Assets/Fonts/OpenSans-Bold.ttf", 24);

		float2 s(camera->FramebufferWidth(), camera->FramebufferHeight());
		float2 c = mInput->CursorPos();
		c.y = s.y - c.y;
		
		if (mShowPerformance) {
			char tmpText[64];

			float graphHeight = 100;

			#ifdef PROFILER_ENABLE
			const uint32_t pointCount = PROFILER_FRAME_COUNT - 1;

			float2 points[pointCount];
			float m = 0;
			for (uint32_t i = 0; i < pointCount; i++) {
				points[i].y = (mSnapshotPerformance ? mProfilerFrames[i] : Profiler::Frames()[(i + Profiler::CurrentFrameIndex() + 2) % PROFILER_FRAME_COUNT]).mDuration.count() * 1e-6f;
				points[i].x = (float)i / (pointCount - 1.f);
				m = fmaxf(points[i].y, m);
			}
			m = fmaxf(m, 5.f) + 3.f;
			for (uint32_t i = 0; i < pointCount; i++)
				points[i].y /= m;

			GUI::Rect(float2(0, 0), float2(s.x, graphHeight), float4(.1f, .1f, .1f, 1));
			GUI::Rect(float2(0, graphHeight - 1), float2(s.x, 2), float4(.2f, .2f, .2f, 1));

			snprintf(tmpText, 64, "%.1fms", m);
			GUI::DrawString(sem11, tmpText, float4(.6f, .6f, .6f, 1.f), float2(2, graphHeight - 10), 11.f);

			for (float i = 1; i < 3; i++) {
				float x = m * i / 3.f;
				snprintf(tmpText, 32, "%.1fms", x);
				GUI::Rect(float2(0, graphHeight * (i / 3.f) - 1), float2(s.x, 1), float4(.2f, .2f, .2f, 1));
				GUI::DrawString(sem11, tmpText, float4(.6f, .6f, .6f, 1.f), float2(2, graphHeight * (i / 3.f) + 2), 11.f);
			}

			GUI::DrawScreenLine(points, pointCount, 0, float2(s.x, graphHeight), float4(.2f, 1.f, .2f, 1.f));

			if (mSnapshotPerformance) {
				if (c.y < 100) {
					uint32_t hvr = (uint32_t)((c.x / s.x) * (PROFILER_FRAME_COUNT - 2) + .5f);
					GUI::Rect(float2(s.x * hvr / (PROFILER_FRAME_COUNT - 2), 0), float2(1, graphHeight), float4(1, 1, 1, .15f));
					if (mInput->KeyDown(MOUSE_LEFT))
						mSelectedFrame = hvr;
				}

				if (mSelectedFrame < PROFILER_FRAME_COUNT - 1) {
					ProfilerSample* selected = nullptr;
					float sampleHeight = 20;

					// selection line
					GUI::Rect(float2(s.x * mSelectedFrame / (PROFILER_FRAME_COUNT - 2), 0), float2(1, graphHeight), 1);

					float id = 1.f / (float)mProfilerFrames[mSelectedFrame].mDuration.count();

					queue<pair<ProfilerSample*, uint32_t>> samples;
					samples.push(make_pair(mProfilerFrames + mSelectedFrame, 0));
					while (samples.size()) {
						auto p = samples.front();
						samples.pop();

						float2 pos(s.x * (p.first->mStartTime - mProfilerFrames[mSelectedFrame].mStartTime).count() * id, graphHeight + 20 + sampleHeight * p.second);
						float2 size(s.x * (float)p.first->mDuration.count() * id, sampleHeight);
						float4 col(0, 0, 0, 1);

						if (c.x > pos.x&& c.y > pos.y && c.x < pos.x + size.x && c.y < pos.y + size.y) {
							selected = p.first;
							col.rgb = 1;
						}

						GUI::Rect(pos, size, col);
						GUI::Rect(pos + 1, size - 2, float4(.3f, .9f, .3f, 1));

						for (auto it = p.first->mChildren.begin(); it != p.first->mChildren.end(); it++)
							samples.push(make_pair(&*it, p.second + 1));
					}

					if (selected) {
						snprintf(tmpText, 64, "%s: %.2fms\n", selected->mLabel, selected->mDuration.count() * 1e-6f);
						GUI::Rect(float2(0, graphHeight), float2(s.x, 20), float4(0,0,0,.8f));
						GUI::DrawString(reg14, tmpText, 1, float2(s.x * .5f, graphHeight + 8), 14.f, TEXT_ANCHOR_MID, TEXT_ANCHOR_MID);
					}
				}

			}
			#endif

			snprintf(tmpText, 64, "%.2f fps | %llu tris\n", mFps, commandBuffer->mTriangleCount);
			GUI::DrawString(sem16, tmpText, 1.f, float2(5, camera->FramebufferHeight() - 18), 18.f);
		}

		float2 panelSize(300, 400);
		float2 panelPos(10, s.y * .5f - panelSize.y * .5f);

		float2 scrollViewSize(panelSize.x - 10, panelSize.y - 70);
		float2 scrollViewPos(panelPos.x+5, panelPos.y+35);

		GUI::Rect(panelPos-2, panelSize+4, float4(.3f, .3f, .3f, 1)); // outline
		GUI::Rect(panelPos, panelSize, float4(.2f, .2f, .2f, 1)); // background
		GUI::Label(bld24, "Load DICOM", 24, panelPos+float2(0, panelSize.y-35), float2(panelSize.x, 30), 0, 1);
		GUI::Rect(panelPos + float2(0, panelSize.y - 30), float2(panelSize.x, 1), 1); // separator

		GUI::BeginScrollLayout("FolderScroll", LAYOUT_VERTICAL,); // scroll view

		float y = scrollViewSize.y + mFolderScrollAmount - 24;
		for (const fs::path& p : mDicomFolders) {
			if (GUI::Button(sem16, p.stem().string(), 16, scrollViewPos+float2(0, y), float2(scrollViewSize.x, 20), float4(.3f, .3f, .3f, 1), 1, TEXT_ANCHOR_MIN, TEXT_ANCHOR_MID, float4(scrollViewPos, scrollViewSize)))
				LoadVolume(commandBuffer, p);
			y -= 20;
		}
	}

	PLUGIN_EXPORT void PostProcess(CommandBuffer* commandBuffer, Camera* camera) override {
		if (!mRawVolume) return;

		if (mRawVolumeNew) {
			mRawVolume->TransitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, commandBuffer);
			mRawVolumeNew = false;
		}

		FrameData& fd = mFrameData[commandBuffer->Device()->FrameContextIndex()];
		if (fd.mImagesNew) {
			fd.mBakedVolume->TransitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, commandBuffer);
			fd.mOpticalDensity->TransitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, commandBuffer);
			fd.mImagesNew = false;
		}
		
		float2 res(camera->FramebufferWidth(), camera->FramebufferHeight());
		float4x4 ivp = camera->InverseViewProjection();
		float3 cp = camera->WorldPosition();
		float4 ivr = inverse(mVolumeRotation).xyzw;
		float3 ivs = 1.f / mVolumeScale;
		float far = camera->Far();
		float3 vres(fd.mBakedVolume->Width(), fd.mBakedVolume->Height(), fd.mBakedVolume->Depth());

		float threshold = .125f;
		float invThreshold = 1 / (1 - .125f);
		float density = 100.f;
		float stepSize = .002f;

		float scatter = 100.f;
		float extinction = 20.f;

		float3 lightCol = 2;
		float3 lightDir = normalize(float3(.1f, .5f, -1));

		#pragma region pre-process
		ComputeShader* process = mScene->AssetManager()->LoadShader("Shaders/volume.stm")->GetCompute("PreProcess", {});
		vkCmdBindPipeline(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, process->mPipeline);
		
		DescriptorSet* ds = commandBuffer->Device()->GetTempDescriptorSet("PreProcess", process->mDescriptorSetLayouts[0]);
		ds->CreateStorageTextureDescriptor(mRawVolume, process->mDescriptorBindings.at("RawVolume").second.binding, VK_IMAGE_LAYOUT_GENERAL);
		ds->CreateStorageTextureDescriptor(fd.mBakedVolume, process->mDescriptorBindings.at("BakedVolume").second.binding, VK_IMAGE_LAYOUT_GENERAL);
		ds->CreateStorageTextureDescriptor(fd.mOpticalDensity, process->mDescriptorBindings.at("BakedOpticalDensity").second.binding, VK_IMAGE_LAYOUT_GENERAL);
		ds->FlushWrites();

		commandBuffer->PushConstant(process, "InvVolumeRotation", &ivr);
		commandBuffer->PushConstant(process, "InvVolumeScale", &ivs);
		commandBuffer->PushConstant(process, "InvViewProj", &ivp);
		commandBuffer->PushConstant(process, "VolumeResolution", &vres);

		commandBuffer->PushConstant(process, "LightDirection", &lightDir);
		commandBuffer->PushConstant(process, "LightColor", &lightCol);

		commandBuffer->PushConstant(process, "Threshold", &threshold);
		commandBuffer->PushConstant(process, "InvThreshold", &invThreshold);
		commandBuffer->PushConstant(process, "Density", &density);

		commandBuffer->PushConstant(process, "StepSize", &stepSize);
		commandBuffer->PushConstant(process, "FrameIndex", &mFrameIndex);

		vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, process->mPipelineLayout, 0, 1, *ds, 0, nullptr);
		vkCmdDispatch(*commandBuffer, (mRawVolume->Width() + 3) / 4, (mRawVolume->Height() + 3) / 4, (mRawVolume->Depth() + 3) / 4);
		#pragma endregion

		stepSize = .001f;

		fd.mBakedVolume->TransitionImageLayout(VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, commandBuffer);
		fd.mOpticalDensity->TransitionImageLayout(VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, commandBuffer);
		
		#pragma region pre-process
		ComputeShader* draw = mScene->AssetManager()->LoadShader("Shaders/volume.stm")->GetCompute("Draw", {});
		vkCmdBindPipeline(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, draw->mPipeline);
		
		ds = commandBuffer->Device()->GetTempDescriptorSet("Draw Volume", draw->mDescriptorSetLayouts[0]);
		ds->CreateSampledTextureDescriptor(fd.mBakedVolume, draw->mDescriptorBindings.at("BakedVolumeS").second.binding, VK_IMAGE_LAYOUT_GENERAL);
		ds->CreateSampledTextureDescriptor(fd.mOpticalDensity, draw->mDescriptorBindings.at("BakedOpticalDensityS").second.binding, VK_IMAGE_LAYOUT_GENERAL);
		ds->CreateStorageTextureDescriptor(camera->ResolveBuffer(0), draw->mDescriptorBindings.at("RenderTarget").second.binding, VK_IMAGE_LAYOUT_GENERAL);
		ds->CreateStorageTextureDescriptor(camera->ResolveBuffer(1), draw->mDescriptorBindings.at("DepthNormal").second.binding, VK_IMAGE_LAYOUT_GENERAL);
		ds->CreateSampledTextureDescriptor(mScene->AssetManager()->LoadTexture("Assets/Textures/rgbanoise.png", false), draw->mDescriptorBindings.at("NoiseTex").second.binding);
		ds->FlushWrites();

		commandBuffer->PushConstant(draw, "VolumePosition", &mVolumePosition);
		commandBuffer->PushConstant(draw, "InvVolumeRotation", &ivr);
		commandBuffer->PushConstant(draw, "InvVolumeScale", &ivs);
		commandBuffer->PushConstant(draw, "InvViewProj", &ivp);
		commandBuffer->PushConstant(draw, "CameraPosition", &cp);
		commandBuffer->PushConstant(draw, "ScreenResolution", &res);
		commandBuffer->PushConstant(draw, "VolumeResolution", &vres);
		commandBuffer->PushConstant(draw, "Far", &far);
		
		commandBuffer->PushConstant(draw, "LightDirection", &lightDir);
		commandBuffer->PushConstant(draw, "LightColor", &lightCol);

		commandBuffer->PushConstant(draw, "Threshold", &threshold);
		commandBuffer->PushConstant(draw, "InvThreshold", &invThreshold);
		commandBuffer->PushConstant(draw, "Density", &density);
		commandBuffer->PushConstant(draw, "Extinction", &extinction);
		commandBuffer->PushConstant(draw, "Scattering", &scatter);

		commandBuffer->PushConstant(draw, "StepSize", &stepSize);
		commandBuffer->PushConstant(draw, "FrameIndex", &mFrameIndex);

		float4x4 InvViewProj;
		float3 CameraPosition;
		float2 ScreenResolution;
		float3 VolumeResolution;
		float Far;

		vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, draw->mPipelineLayout, 0, 1, *ds, 0, nullptr);
		vkCmdDispatch(*commandBuffer, (camera->FramebufferWidth() + 7) / 8, (camera->FramebufferHeight() + 7) / 8, 1);

		#pragma endregion

		mFrameIndex++;
	}
	
	void LoadVolume(CommandBuffer* commandBuffer, const fs::path& folder){
		safe_delete(mRawVolume);

		mVolumeRotation = quaternion(0,0,0,1);
		mVolumePosition = float3(0, 1.6f, 0);
		mRawVolume = Dicom::LoadDicomStack(folder.string(), mScene->Instance()->Device(), &mVolumeScale);
		mRawVolumeNew = true;

		for (uint32_t i = 0; i < commandBuffer->Device()->MaxFramesInFlight(); i++) {
			FrameData& fd = mFrameData[i];
			safe_delete(fd.mBakedVolume);
			safe_delete(fd.mOpticalDensity);
			fd.mBakedVolume = new Texture("Baked Volume", mScene->Instance()->Device(), mRawVolume->Width(), mRawVolume->Height(), mRawVolume->Depth(), VK_FORMAT_R16_UNORM, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
			fd.mOpticalDensity = new Texture("Baked Optical Density", mScene->Instance()->Device(), mRawVolume->Width(), mRawVolume->Height(), mRawVolume->Depth(), VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
			fd.mImagesNew = true;
		}
	}
};

ENGINE_PLUGIN(DicomVis)