#pragma once

#include <Content/Shader.hpp>
#include <Core/Buffer.hpp>
#include <Scene/Renderer.hpp>
#include <Scene/Scene.hpp>
#include <Util/Util.hpp>

class TerrainRenderer : public Renderer {
public:
	bool mVisible;

	PLUGIN_EXPORT TerrainRenderer(const std::string& name, float size, float height);
	PLUGIN_EXPORT ~TerrainRenderer();

	inline float Height() const { return mHeight; }
	inline float Size() const { return mSize; }

	inline bool CastShadows() override { return true; }

	inline ::Material* Material() const { return mMaterial.get(); }
	inline void Material(std::shared_ptr<::Material> m) { mMaterial = m; }

	inline bool Visible() override { return mVisible && EnabledHierarchy(); }
	inline uint32_t RenderQueue() override { return mMaterial ? mMaterial->RenderQueue() : 1000; }
	PLUGIN_EXPORT void Draw(CommandBuffer* commandBuffer, Camera* camera, PassType pass) override;
	PLUGIN_EXPORT void DrawGizmos(CommandBuffer* commandBuffer, Camera* camera) override;
	
	inline virtual AABB Bounds() override { UpdateTransform(); return mAABB; }

private:
	struct QuadNode {
		static const uint32_t Resolution = 16;

		TerrainRenderer* mTerrain;

		QuadNode* mParent;
		QuadNode* mChildren;

		uint32_t mSiblingIndex;
		uint32_t mLod;
		float2 mPosition; // 0-1

		float mSize;
		float mVertexResolution;

		uint8_t mTriangleMask;

		inline QuadNode() : mTerrain(nullptr), mParent(nullptr), mChildren(nullptr), mTriangleMask(0), mSiblingIndex(0), mLod(0), mVertexResolution(0), mSize(0) {}
		PLUGIN_EXPORT QuadNode(TerrainRenderer* terrain, QuadNode* parent, uint32_t siblingIndex, uint32_t lod, const float2& pos, float size);
		PLUGIN_EXPORT ~QuadNode();

		PLUGIN_EXPORT bool ShouldSplit(const float2& camPos);

		PLUGIN_EXPORT void Split();
		PLUGIN_EXPORT void Join();

		PLUGIN_EXPORT void ComputeTriangleFanMask(bool recurse = true);
		PLUGIN_EXPORT void UpdateNeighbors();

		PLUGIN_EXPORT QuadNode* LeftNeighbor();
		PLUGIN_EXPORT QuadNode* RightNeighbor();
		PLUGIN_EXPORT QuadNode* ForwardNeighbor();
		PLUGIN_EXPORT QuadNode* BackNeighbor();
	};

	std::unordered_map<Device*, Buffer*> mIndexBuffers;
	std::vector<uint32_t> mIndexOffsets;
	std::vector<uint32_t> mIndexCounts;

	float mSize;
	float mHeight;
	float mMaxVertexResolution;

    std::shared_ptr<::Material> mMaterial;
	AABB mAABB;

protected:
	virtual bool UpdateTransform() override;
};