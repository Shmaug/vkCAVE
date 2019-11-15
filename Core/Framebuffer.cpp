#include <Core/CommandBuffer.hpp>
#include <Core/Framebuffer.hpp>

using namespace std;

Framebuffer::Framebuffer(const string& name, ::Device* device, uint32_t width, uint32_t height, const vector<VkFormat>& colorFormats, VkFormat depthFormat, VkSampleCountFlagBits sampleCount)
	: mName(name), mDevice(device), mWidth(width), mHeight(height), mSampleCount(sampleCount), mColorFormats(colorFormats), mDepthFormat(depthFormat) {

	mFramebuffers = new VkFramebuffer[mDevice->MaxFramesInFlight()];
	mColorBuffers = new vector<Texture*>[mDevice->MaxFramesInFlight()];
	mDepthBuffers = new Texture * [mDevice->MaxFramesInFlight()];

	memset(mFramebuffers, VK_NULL_HANDLE, sizeof(VkFramebuffer) * mDevice->MaxFramesInFlight());
	memset(mDepthBuffers, 0, sizeof(Texture*) * mDevice->MaxFramesInFlight());

	for (uint32_t i = 0; i < mDevice->MaxFramesInFlight(); i++) {
		mColorBuffers[i] = vector<Texture*>(colorFormats.size());
		memset(mColorBuffers[i].data(), 0, sizeof(Texture*) * colorFormats.size());
	}

	mClearValues.resize(mColorFormats.size() + 1);
	for (uint32_t i = 0; i < mColorFormats.size(); i++)
		mClearValues[i] = { .0f, .0f, .0f, 0.f };
	mClearValues[mColorFormats.size()] = { 1.f, 0.f };

	vector<VkAttachmentReference> colorAttachments(colorFormats.size());
	vector<VkAttachmentDescription> attachments(colorFormats.size() + 1);
	for (uint32_t i = 0; i < colorFormats.size(); i++) {
		attachments[i].format = colorFormats[i];
		attachments[i].samples = mSampleCount;
		attachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[i].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		colorAttachments[i].attachment = i;
		colorAttachments[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	}
	attachments[colorFormats.size()].format = mDepthFormat;
	attachments[colorFormats.size()].samples = mSampleCount;
	attachments[colorFormats.size()].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[colorFormats.size()].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[colorFormats.size()].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	attachments[colorFormats.size()].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[colorFormats.size()].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[colorFormats.size()].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depthAttachmentRef = {};
	depthAttachmentRef.attachment = (uint32_t)colorFormats.size();
	depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	vector<VkSubpassDescription> subpasses(1);
	subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpasses[0].colorAttachmentCount = (uint32_t)mColorFormats.size();
	subpasses[0].pColorAttachments = colorAttachments.data();
	subpasses[0].pDepthStencilAttachment = &depthAttachmentRef;

	mRenderPass = new ::RenderPass(mName + "RenderPass", this, attachments, subpasses);
	mUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
}
Framebuffer::~Framebuffer() {
	safe_delete(mRenderPass);
	for (uint32_t i = 0; i < mDevice->MaxFramesInFlight(); i++) {
		if (mFramebuffers[i] != VK_NULL_HANDLE)
			vkDestroyFramebuffer(*mDevice, mFramebuffers[i], nullptr);
		for (Texture* t : mColorBuffers[i])
			safe_delete(t);
		safe_delete(mDepthBuffers[i]);
	}
	safe_delete_array(mColorBuffers);
	safe_delete_array(mDepthBuffers);
	safe_delete_array(mFramebuffers);
}

bool Framebuffer::UpdateBuffers() {
	uint32_t frameContextIndex = mDevice->FrameContextIndex();
	if (mFramebuffers[frameContextIndex] == VK_NULL_HANDLE
		|| mDepthBuffers[frameContextIndex]->Width() != mWidth || mDepthBuffers[frameContextIndex]->Height() != mHeight || mColorBuffers[frameContextIndex][0]->Usage() != mUsage) {

		if (mFramebuffers[frameContextIndex] != VK_NULL_HANDLE)
			vkDestroyFramebuffer(*mDevice, mFramebuffers[frameContextIndex], nullptr);

		vector<VkImageView> views(mColorBuffers[frameContextIndex].size() + 1);

		for (uint32_t i = 0; i < mColorFormats.size(); i++) {
			safe_delete(mColorBuffers[frameContextIndex][i]);
			mColorBuffers[frameContextIndex][i] = new Texture(mName + "ColorBuffer", mDevice, mWidth, mHeight, 1, mColorFormats[i], mSampleCount, VK_IMAGE_TILING_OPTIMAL, mUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			views[i] = mColorBuffers[frameContextIndex][i]->View(mDevice);
		}

		safe_delete(mDepthBuffers[frameContextIndex]);
		mDepthBuffers[frameContextIndex] = new Texture(mName + "DepthBuffer", mDevice, mWidth, mHeight, 1, mDepthFormat, mSampleCount, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		views[mColorBuffers[frameContextIndex].size()] = mDepthBuffers[frameContextIndex]->View(mDevice);

		VkFramebufferCreateInfo fb = {};
		fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fb.attachmentCount = (uint32_t)views.size();
		fb.pAttachments = views.data();
		fb.renderPass = *mRenderPass;
		fb.width = mWidth;
		fb.height = mHeight;
		fb.layers = 1;
		vkCreateFramebuffer(*mDevice, &fb, nullptr, &mFramebuffers[frameContextIndex]);
		mDevice->SetObjectName(mFramebuffers[frameContextIndex], mName + " Framebuffer " + to_string(frameContextIndex), VK_OBJECT_TYPE_FRAMEBUFFER);

		return true;
	}
	return false;
}

void Framebuffer::BeginRenderPass(CommandBuffer* commandBuffer) {
	uint32_t frameContextIndex = mDevice->FrameContextIndex();
	if (UpdateBuffers()){
		for (uint32_t i = 0; i < mColorFormats.size(); i++)
			mColorBuffers[frameContextIndex][i]->TransitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, commandBuffer);
		mDepthBuffers[frameContextIndex]->TransitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, commandBuffer);
	}

	commandBuffer->BeginRenderPass(mRenderPass, { mWidth, mHeight }, mFramebuffers[frameContextIndex], mClearValues.data(), (uint32_t)mClearValues.size());
}
