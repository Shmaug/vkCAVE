#pragma once

#include <Core/EnginePlugin.hpp>
#include <Scene/TextRenderer.hpp>

class CameraControl : public EnginePlugin {
public:
	PLUGIN_EXPORT CameraControl();
	PLUGIN_EXPORT ~CameraControl();

	PLUGIN_EXPORT bool Init(Scene* scene) override;
	PLUGIN_EXPORT void Update(const FrameTime& frameTime) override;

	inline void CameraDistance(float d) { mCameraDistance = d; }
	inline float CameraDistance() const { return mCameraDistance; }
	inline Object* CameraPivot() const { return mCameraPivot; }

private:
	Scene* mScene;
	Object* mCameraPivot;
	float mCameraDistance;
	vec3 mCameraEuler;

	TextRenderer* mFpsText;

	float mFrameTimeAccum;
	float mFps;
	uint32_t mFrameCount;
};