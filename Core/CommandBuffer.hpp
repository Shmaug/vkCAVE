#pragma once

#include <Util/Util.hpp>

#ifdef ENABLE_DEBUG_LAYERS
#define BEGIN_CMD_REGION(cmd, label) cmd->BeginLabel(label)
#define BEGIN_CMD_REGION_COLOR(cmd, label, color) cmd->BeginLabel(label, color)
#define END_CMD_REGION(cmd) cmd->EndLabel()
#else
#define BEGIN_CMD_REGION(cmd, label)
#define END_CMD_REGION(cmd)
#endif

class Device;
class Material;
class GraphicsShader;
class RenderPass;
class Camera;
class ShaderVariant;

class Fence {
public:
	ENGINE_EXPORT Fence(Device* device);
	ENGINE_EXPORT ~Fence();
	ENGINE_EXPORT void Wait();
	ENGINE_EXPORT bool Signaled();
	ENGINE_EXPORT void Reset();
	inline operator VkFence() const { return mFence; }
private:
	Device* mDevice;
	VkFence mFence;
};

class Semaphore {
public:
	ENGINE_EXPORT Semaphore(Device* device);
	ENGINE_EXPORT ~Semaphore();
	inline operator VkSemaphore() const { return mSemaphore; }
private:
	Device* mDevice;
	VkSemaphore mSemaphore;
};

class CommandBuffer {
public:
	ENGINE_EXPORT ~CommandBuffer();
	inline operator VkCommandBuffer() const { return mCommandBuffer; }

	#ifdef ENABLE_DEBUG_LAYERS
	ENGINE_EXPORT void BeginLabel(const std::string& label, const float4& color = float4(1,1,1,0));
	ENGINE_EXPORT void EndLabel();
	#endif

	ENGINE_EXPORT void Reset(const std::string& name = "Command Buffer");

	inline RenderPass* CurrentRenderPass() const { return mCurrentRenderPass; }

	ENGINE_EXPORT bool PushConstant(ShaderVariant* shader, const std::string& name, const void* value);

	ENGINE_EXPORT VkPipelineLayout BindShader(GraphicsShader* shader, const VertexInput* input, Camera* camera = nullptr,
		VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		VkCullModeFlags cullMode = VK_CULL_MODE_FLAG_BITS_MAX_ENUM,
		BlendMode blendMode = BLEND_MODE_MAX_ENUM,
		VkPolygonMode polyMode = VK_POLYGON_MODE_MAX_ENUM);
	ENGINE_EXPORT VkPipelineLayout BindMaterial(Material* material, const VertexInput* input, Camera* camera = nullptr,
		VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		VkCullModeFlags cullMode = VK_CULL_MODE_FLAG_BITS_MAX_ENUM,
		BlendMode blendMode = BLEND_MODE_MAX_ENUM,
		VkPolygonMode polyMode = VK_POLYGON_MODE_MAX_ENUM);
	ENGINE_EXPORT void BeginRenderPass(RenderPass* renderPass, const VkExtent2D& bufferSize, VkFramebuffer frameBuffer, VkClearValue* clearValues, uint32_t clearValueCount);
	ENGINE_EXPORT void EndRenderPass();

	inline ::Device* Device() const { return mDevice; }

	uint32_t mTriangleCount;

private:
	friend class Device;
	ENGINE_EXPORT CommandBuffer(::Device* device, VkCommandPool commandPool, const std::string& name = "Command Buffer");
	::Device* mDevice;
	VkCommandBuffer mCommandBuffer;
	VkCommandPool mCommandPool;
	std::shared_ptr<Fence> mSignalFence;
	std::vector<std::shared_ptr<Semaphore>> mSignalSemaphores;

	RenderPass* mCurrentRenderPass;
	Camera* mCurrentCamera;
	VkPipeline mCurrentPipeline;
	Material* mCurrentMaterial;
};