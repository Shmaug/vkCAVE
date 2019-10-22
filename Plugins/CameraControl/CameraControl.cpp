#include "CameraControl.hpp"
#include <Interface/UICanvas.hpp>
#include <Scene/Scene.hpp>
#include <Util/Profiler.hpp>

using namespace std;

ENGINE_PLUGIN(CameraControl)

CameraControl::CameraControl()
	: mScene(nullptr), mDragging(false), mCameraPivot(nullptr), mFpsText(nullptr), mInput(nullptr), mCameraDistance(1.5f), mCameraEuler(float3(0)), mFps(0), mFrameTimeAccum(0), mFrameCount(0) {
	mEnabled = true;
}
CameraControl::~CameraControl() {
	mScene->RemoveObject(mCameraPivot);
	mScene->RemoveObject(mFpsText);
}

bool CameraControl::Init(Scene* scene) {
	mScene = scene;

	mInput = mScene->InputManager()->GetFirst<MouseKeyboardInput>();

	Shader* fontshader = mScene->AssetManager()->LoadShader("Shaders/font.shader");
	Font* font = mScene->AssetManager()->LoadFont("Assets/OpenSans-Regular.ttf", 36);

	shared_ptr<TextRenderer> fpsText = make_shared<TextRenderer>("Fps Text");
	fpsText->Font(font);
	fpsText->Text("");
	fpsText->VerticalAnchor(Maximum);
	fpsText->HorizontalAnchor(Minimum);
	mScene->AddObject(fpsText);
	mFpsText = fpsText.get();

	shared_ptr<Object> cameraPivot = make_shared<Object>("CameraPivot");
	mScene->AddObject(cameraPivot);
	mCameraPivot = cameraPivot.get();
	mCameraPivot->LocalPosition(0, .5f, 0);

	for (auto& camera : mScene->Cameras()) {
		mCameraPivot->AddChild(camera);
		camera->LocalPosition(0, 0, -mCameraDistance);
	}

	return true;
}

void CameraControl::Update(const FrameTime& frameTime) {
	if (mInput->KeyDownFirst(GLFW_KEY_F1))
		mScene->DrawGizmos(!mScene->DrawGizmos());
	
	Camera* c = mScene->Cameras()[0];
	c->AddChild(mFpsText);
	float d = c->Near() + .001f;
	float y = d * tanf(c->FieldOfView() * .5f);
	float x = y * c->Aspect();
	mFpsText->LocalPosition(x * (-1.f + 32.f / c->PixelWidth()), y * (1.f - 32.f / c->PixelHeight()), d);
	mFpsText->TextScale(d * .015f);

	mCameraDistance = fmaxf(mCameraDistance * (1 - mInput->ScrollDelta().y * .06f), .025f);

	if (mInput->MouseButtonDownFirst(GLFW_MOUSE_BUTTON_LEFT)) {
		Collider* hit = mScene->Raycast(mInput->GetPointer(0)->mWorldRay);
		mDragging = !dynamic_cast<UICanvas*>(hit);
	}
	if (mDragging && !mInput->MouseButtonDown(GLFW_MOUSE_BUTTON_LEFT)) mDragging = false;
	if (mDragging) {
		float3 md = float3(mInput->CursorDelta(), 0);
		if (mInput->KeyDown(GLFW_KEY_LEFT_SHIFT)) {
			md.x = -md.x;
			md = md * .0005f * mCameraDistance;
		} else
			md = float3(md.y, md.x, 0) * .005f;

		if (mInput->KeyDown(GLFW_KEY_LEFT_SHIFT))
			// translate camera
			mCameraPivot->LocalPosition(mCameraPivot->LocalPosition() + mCameraPivot->LocalRotation() * md);
		else {
			mCameraEuler += md;
			mCameraEuler.x = clamp(mCameraEuler.x, -PI * .5f, PI * .5f);
			// rotate camera
		}
		mCameraPivot->LocalRotation(quaternion(mCameraEuler));
	}

	for (uint32_t i = 0; i < mCameraPivot->ChildCount(); i++)
		if (Camera* c = dynamic_cast<Camera*>(mCameraPivot->Child(i)))
			c->LocalPosition(0, 0, -mCameraDistance);

	mFrameTimeAccum += frameTime.mDeltaTime;
	mFrameCount++;
	if (mFrameTimeAccum > 1.f) {
		mFps = mFrameCount / mFrameTimeAccum;
		mFrameTimeAccum -= 1.f;
		mFrameCount = 0;
		char buf[256];
		sprintf(buf, "%.2f fps\n", mFps);
		mFpsText->Text(buf);
	}
}