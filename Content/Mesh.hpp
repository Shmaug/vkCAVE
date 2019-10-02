#pragma once

#include <Content/Asset.hpp>
#include <Core/Buffer.hpp>
#include <Util/Geometry.hpp>
#include <Core/DeviceManager.hpp>
#include <Util/Util.hpp>

#include <assimp/mesh.h>

class AssetDatabase;

class Mesh : public Asset {
public:
	struct MaterialData {
		std::string mName;
		std::string mDiffuseTexture;
		std::string mNormalTexture;
	};

	const std::string mName;

	ENGINE_EXPORT Mesh(const std::string& name);
	ENGINE_EXPORT Mesh(const std::string& name, ::DeviceManager* devices, const aiMesh* aimesh, float scale = 1.f);
	ENGINE_EXPORT Mesh(const std::string& name, ::DeviceManager* devices, const void* vertices, const void* indices, uint32_t vertexCount, uint32_t vertexSize, uint32_t indexCount, const ::VertexInput* vertexInput, VkIndexType indexType);
	ENGINE_EXPORT Mesh(const std::string& name, ::Device* device, const void* vertices, const void* indices, uint32_t vertexCount, uint32_t vertexSize, uint32_t indexCount, const ::VertexInput* vertexInput, VkIndexType indexType);
	ENGINE_EXPORT ~Mesh() override;

	inline std::shared_ptr<Buffer> VertexBuffer(Device* device) const { return mDeviceData.at(device).mVertexBuffer; }
	inline std::shared_ptr<Buffer> IndexBuffer(Device* device) const { return mDeviceData.at(device).mIndexBuffer; }

	inline uint32_t IndexCount() const { return mIndexCount; }
	inline uint32_t VertexCount() const { return mVertexCount; }
	inline VkIndexType IndexType() const { return mIndexType; }

	inline const ::VertexInput* VertexInput() const { return mVertexInput; }

	inline AABB Bounds() const { return mBounds; }
	inline void Bounds(const AABB& b) { mBounds = b; }

private:
	friend class AssetDatabase;
	ENGINE_EXPORT Mesh(const std::string& name, ::DeviceManager* devices, const std::string& filename, float scale = 1.f);

	const ::VertexInput* mVertexInput;
	uint32_t mIndexCount;
	uint32_t mVertexCount;
	VkIndexType mIndexType;

	AABB mBounds;
	struct DeviceData {
		std::shared_ptr<Buffer> mVertexBuffer;
		std::shared_ptr<Buffer> mIndexBuffer;
	};
	std::unordered_map<Device*, DeviceData> mDeviceData;
};