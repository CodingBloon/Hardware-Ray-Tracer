#pragma once
#include "../vulkan_core/Buffer.h"
#include <string>
#include <vector>
#include <iostream>
#include <span>

namespace RayTracing {

	struct AccelerationStructure {
		VkAccelerationStructureKHR handle;
		VkBuffer buffer;
		VkDeviceMemory memory;
		VkDeviceAddress address;
	};

	struct Vertex {
		float pos[3];
	};

	struct Mesh {
		Mesh(std::vector<Vertex> vertices, std::vector<uint32_t> indices);

		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;

		std::unique_ptr<Core::Buffer> vertexBuffer;
		std::unique_ptr<Core::Buffer> indexBuffer;
	};

	class Scene {
	public:
		Scene(Core::Device& device);
		~Scene();

		void loadAndAppendModel(std::string path);
		void buildAccelerationStructures();
		AccelerationStructure& getTlas() { return tlasAccel; }
	private:
		void primitiveToGeometry(const Mesh& mesh, VkAccelerationStructureGeometryKHR& geometry, VkAccelerationStructureBuildRangeInfoKHR& rangeInfo);
		void createBottomLevelAS();
		void createTopLevelAS();
		void createAccelerationStructure(VkAccelerationStructureTypeKHR asType,
			AccelerationStructure& accelStructure,
			VkAccelerationStructureGeometryKHR& asGeometry,
			VkAccelerationStructureBuildRangeInfoKHR& asBuildRangeInfo,
			VkBuildAccelerationStructureFlagsKHR flags);

	private:
		Core::Device& device;

		std::vector<Mesh> meshes;
		std::vector<Mesh> readyForBuild;
		std::vector<AccelerationStructure> blasAccel;
		AccelerationStructure tlasAccel;

		const VkTransformMatrixKHR transformMatrix = {
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f };
	};

}