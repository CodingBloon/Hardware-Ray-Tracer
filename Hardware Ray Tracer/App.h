#pragma once
#include "Graphics/Window.h"
#include "Graphics/vulkan_core/Device.h"
#include "Graphics/vulkan_core/SwapChain.h"
#include "Graphics/vulkan_core/Buffer.h"
#include "Graphics/vulkan_core/Descriptors.h"
#include "Graphics/Camera.h"
#include <glm/glm.hpp>
#include <span>
#include <array>
#include <fstream>
#include <chrono>

#define vkCmdBuildAccelerationStructuresKHR reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(vkGetDeviceProcAddr(device.getDevice(), "vkCmdBuildAccelerationStructuresKHR"))
#define vkCreateAccelerationStructureKHR reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(vkGetDeviceProcAddr(device.getDevice(), "vkCreateAccelerationStructureKHR"))
#define vkGetAccelerationStructureBuildSizesKHR reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(vkGetDeviceProcAddr(device.getDevice(), "vkGetAccelerationStructureBuildSizesKHR"))
#define vkDestroyAccelerationStructureKHR reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(vkGetDeviceProcAddr(device.getDevice(), "vkDestroyAccelerationStructureKHR"))
#define vkCreateRayTracingPipelinesKHR reinterpret_cast<PFN_vkCreateRayTracingPipelinesKHR>(vkGetDeviceProcAddr(device.getDevice(), "vkCreateRayTracingPipelinesKHR"))
#define vkGetRayTracingShaderGroupHandlesKHR reinterpret_cast<PFN_vkGetRayTracingShaderGroupHandlesKHR>(vkGetDeviceProcAddr(device.getDevice(), "vkGetRayTracingShaderGroupHandlesKHR"))
#define vkCmdTraceRaysKHR reinterpret_cast<PFN_vkCmdTraceRaysKHR>(vkGetDeviceProcAddr(device.getDevice(), "vkCmdTraceRaysKHR"))
#define vkGetAccelerationStructureDeviceAddressKHR reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(vkGetDeviceProcAddr(device.getDevice(), "vkGetAccelerationStructureDeviceAddressKHR"))


template <typename T, typename... Rest>
void hashCombine(std::size_t& seed, const T& v, const Rest&... rest) {
	seed ^= std::hash<T>{}(v)+0x9e3779b9 + (seed << 6) + (seed >> 2);
	(hashCombine(seed, rest), ...);
};

namespace Core {

	struct Vertex {
		float pos[3];

		bool operator==(const Vertex& other) const {
			return pos[0] == other.pos[0] && pos[1] == other.pos[1] && pos[2] == other.pos[2];
		}
	};

	enum BindingPoints {
		eTextures = 0,
		eOutImage,
		eTlas
	};

	struct AccelerationStructure {
		VkAccelerationStructureKHR handle;
		VkBuffer buffer;
		VkDeviceMemory memory;
		VkDeviceAddress address;
	};

	struct Mesh {
		Mesh(Device& device, std::vector<Vertex> vertices, std::vector<uint32_t> indices);

		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;

		std::unique_ptr<Buffer> vertexBuffer;
		std::unique_ptr<Buffer> indexBuffer;
	};

	struct StorageImage {
		VkImage image;
		VkDeviceMemory imageMemory;
		VkImageView imageView;
	};

	struct Uniform {
		glm::mat4 viewInverse;
		glm::mat4 projInverse;
	};

	class App
	{
	public:
		App();
		~App();
		
		void run();

	private:
		// -------------------- RAY TRACING PIPELINE CREATION
		void createRayTracingPipelineLayout();
		void createRayTracingDescriptorSets();
		void createStorageImage();
		void createRayTracingPipeline();
		void createShaderBindingTable(const VkRayTracingPipelineCreateInfoKHR& rtPipelineInfo);

		// -------------------- ACCELERATION STRUCTURE CREATION --------------------
		void primitiveToGeometry(const Mesh& mesh, VkAccelerationStructureGeometryKHR& geometry, VkAccelerationStructureBuildRangeInfoKHR& rangeInfo);
		void createBottomLevelAS();
		void createTopLevelAS();
		void createAccelerationStructure(VkAccelerationStructureTypeKHR asType, 
			AccelerationStructure& accelStructure, 
			VkAccelerationStructureGeometryKHR& asGeometry, 
			VkAccelerationStructureBuildRangeInfoKHR& asBuildRangeInfo, 
			VkBuildAccelerationStructureFlagsKHR flags);

		// -------------------- BUFFER CREATION --------------------
		void createUniformBuffers();

		// -------------------- INPUT SHADER --------------------
		void readShader(std::string file, VkShaderModule* module);
		std::vector<char> readShaderFile(std::string& file);
		void createShaderModule(const std::vector<char>& code, VkShaderModule* module);

		// -------------------- RENDER FUNCTIONS --------------------
		void createCommandBuffers();
		void recreateSwapChain();

		VkCommandBuffer beginFrame();
		void endFrame();
		void beginRenderPass(VkCommandBuffer buffer);
		void endRenderPass(VkCommandBuffer buffer);
		void copyImageToSwapChain(VkCommandBuffer buffer, VkImage swapChainImage, VkImage storageImage, VkExtent2D size);
		void setImageLayout(VkCommandBuffer buffer, VkImage image, VkImageLayout layout, VkImageLayout l);
		void rayTraceScene();

		// -------------------- DESTROY FUNCTIONS --------------------
		void destroyStorageImage();
		void destroyAccelerationStructures();
		void freeCommandBuffers();

		// -------------------- TEST FUNCTIONS --------------------
		void loadModel(std::string path);
		void generateMesh();
	private:

		Window window;
		Device device;
		std::unique_ptr<SwapChain> swapChain;
		Camera camera;

		std::vector<Mesh> meshes;
		std::vector<AccelerationStructure> blasAccel;
		AccelerationStructure tlasAccel;
		StorageImage storageImage;

		std::unique_ptr<DescriptorPool> globalPool{};
		std::unique_ptr<DescriptorSetLayout> globalSetLayout;
		std::vector<VkDescriptorSet> globalDescriptorSets;
		std::vector<std::unique_ptr<Buffer>> uniformBuffers;

		VkPipelineLayout graphicsPipelineLayout;
		VkPipeline graphicsPipeline;

		std::unique_ptr<Buffer> sbtBuffer;
		std::vector<uint8_t> shaderHandles;
		std::vector<char> shaderRawCode;
		VkShaderModule rtShaderModule;

		VkStridedDeviceAddressRegionKHR raygenRegion{};
		VkStridedDeviceAddressRegionKHR missRegion{};
		VkStridedDeviceAddressRegionKHR hitRegion{};
		VkStridedDeviceAddressRegionKHR callableRegion{};

		std::vector<VkCommandBuffer> commandBuffers;

		bool frameStarted;
		uint32_t currentImageIndex;
		uint32_t currentFrameIndex;
	};
}