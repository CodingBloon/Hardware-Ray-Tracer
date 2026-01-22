#pragma once

#include "Debugging.h"
#include "MeshInstance.h"

#include "../vulkan_core/Device.h"
#include "../vulkan_core/Buffer.h"
#include "../tinyobj/tiny_obj_loader.h"
#include <unordered_map>
#include <glm/glm.hpp>

#define vkCmdBuildAccelerationStructuresKHR reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(vkGetDeviceProcAddr(device.getDevice(), "vkCmdBuildAccelerationStructuresKHR"))
#define vkCreateAccelerationStructureKHR reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(vkGetDeviceProcAddr(device.getDevice(), "vkCreateAccelerationStructureKHR"))
#define vkGetAccelerationStructureBuildSizesKHR reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(vkGetDeviceProcAddr(device.getDevice(), "vkGetAccelerationStructureBuildSizesKHR"))
#define vkDestroyAccelerationStructureKHR reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(vkGetDeviceProcAddr(device.getDevice(), "vkDestroyAccelerationStructureKHR"))
#define vkGetAccelerationStructureDeviceAddressKHR reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(vkGetDeviceProcAddr(device.getDevice(), "vkGetAccelerationStructureDeviceAddressKHR"))

#define ROUGHNESS_ZERO 0.0001f

template <typename T, typename... Rest>
void hashCombine(std::size_t& seed, const T& v, const Rest&... rest) {
	seed ^= std::hash<T>{}(v)+0x9e3779b9 + (seed << 6) + (seed >> 2);
	(hashCombine(seed, rest), ...);
};

namespace RayTracing {

	struct Vertex {
		float pos[3];
		float normal[3];
		float uv[2];

		bool operator==(const Vertex& other) const {
			return pos[0] == other.pos[0] && pos[1] == other.pos[1] && pos[2] == other.pos[2] &&
				normal[0] == other.normal[0] && normal[1] == other.normal[1] && normal[2] == other.normal[2] &&
				uv[0] == other.uv[0] && uv[1] == other.uv[1];
		}
	};

	struct Mesh {
		Mesh(Core::Device& device, std::vector<Vertex> vertices, std::vector<uint32_t> indices);

		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;

		std::unique_ptr<Core::Buffer> vertexBuffer;
		std::unique_ptr<Core::Buffer> indexBuffer;
	};

	struct Material {
		float color[3];
		float subsurface;
		float metallic;
		float roughness;
		float specular = 0.5f;
		float specularTint;
		float anisotropic;
		float sheen;
		float sheenTint;
		float clearCoat;
		float clearCoatGloss;
	};

	enum LightType : uint8_t {
		POINT,
		SPOT,
		DIRECTIONAL
	};

	struct Light {
		float pos[3];
		float color[3];
		float intensity;
		LightType type;
	};

	struct AccelerationStructure {
		VkAccelerationStructureKHR handle;
		VkBuffer buffer;
		VkDeviceMemory memory;
		VkDeviceAddress address;
	};

	struct InstanceInfo {
		uint64_t vertexAddress; //address of vertex buffer
		uint64_t indexAddress; //address of index buffer
		uint32_t materialId; //id of material
	};

	struct SkyInfo {
		float skyColor[3];
		float horizonColor[3];
		float groundColor[3];
		float sunDirection[3];
		float upDirection[3];

		float brightness;
		float horizonSize;
		float angularSize;
		float glowIntensity;
		float glowSharpness;
		float glowSize;
		float lightRadiance;
	};

	struct SceneBufferInfo {
		uint64_t mBuf; //address of material buffer
		uint64_t mStride; //byte stride of material

		uint64_t lBuf; //address of light buffer
		uint64_t lStride; //byte stride of light
		uint64_t lCount; //count of lights

		uint64_t vStride; //byte stride of vertices

		uint64_t sBuf; //address of scene buffer
		uint64_t sStride; //byte stride of scene info

		uint64_t skyBuf;
		uint64_t skyStride;
	};

	class Scene {
	public:
		Scene(Core::Device& device);
		~Scene();

		void loadModel(std::string path);
		void createInstance(uint32_t meshId, uint32_t materialId, glm::vec3 position = glm::vec3(), glm::vec3 rotation = glm::vec3(), glm::vec3 scale = glm::vec3(1, 1, 1));
		void createMaterial(glm::vec3 color, float metallic = 0.f, float roughness = 1.f, glm::vec3 emissiveColor = glm::vec3(), float emissionStrength = 0.f);
		void createLight(glm::vec3 position, glm::vec3 color, float intensity);
		void build();
		inline AccelerationStructure getTlas() { return tlasAccel; }
		inline std::unique_ptr<Core::Buffer>& getSceneInfoBuffer() { return sceneInfoBuffer; }

		Scene(const Scene&) = delete;
		Scene operator=(Scene&) = delete;
		Scene(const Scene&&) = delete;
		Scene operator=(Scene&&) = delete;
	private:
		void primitiveToGeometry(const Mesh& mesh, VkAccelerationStructureGeometryKHR& geometry, VkAccelerationStructureBuildRangeInfoKHR& rangeInfo);
		void createBottomAS();
		void createTopAS();
		void createAccelerationStructure(VkAccelerationStructureTypeKHR asType,
			AccelerationStructure& accelStructure,
			VkAccelerationStructureGeometryKHR& asGeometry,
			VkAccelerationStructureBuildRangeInfoKHR& asBuildRangeInfo,
			VkBuildAccelerationStructureFlagsKHR flags);

		void createMaterials();
		void createLights();
		void createSky();
		void createSceneInformation();
		void createSceneInfoBuffer();

		void stageInformation(void* data, uint64_t size, VkBuffer dstBuffer);
	private:
		Core::Device& device;

		std::vector<Mesh> meshes;
		std::vector<MeshInstance> instances;
		std::vector<Material> materials;
		std::vector<Light> lights;
		std::vector<AccelerationStructure> blasAccel;
		AccelerationStructure tlasAccel;

		std::unique_ptr<Core::Buffer> materialBuffer;
		std::unique_ptr<Core::Buffer> lightBuffer;
		std::unique_ptr<Core::Buffer> vertexBuffer;
		std::unique_ptr<Core::Buffer> indexBuffer;
		std::unique_ptr<Core::Buffer> instanceBuffer;
		std::unique_ptr<Core::Buffer> skyBuffer;
		std::unique_ptr<Core::Buffer> sceneInfoBuffer;
	};

}