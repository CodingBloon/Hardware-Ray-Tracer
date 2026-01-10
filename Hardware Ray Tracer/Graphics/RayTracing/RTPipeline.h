#pragma once

#include <array>
#include <fstream>
#include <glm/glm.hpp>
#include "../vulkan_core/Device.h"
#include "../vulkan_core/SwapChain.h"
#include "../vulkan_core/Descriptors.h"
#include "Scene.h"

#define vkCreateRayTracingPipelinesKHR reinterpret_cast<PFN_vkCreateRayTracingPipelinesKHR>(vkGetDeviceProcAddr(device.getDevice(), "vkCreateRayTracingPipelinesKHR"))
#define vkGetRayTracingShaderGroupHandlesKHR reinterpret_cast<PFN_vkGetRayTracingShaderGroupHandlesKHR>(vkGetDeviceProcAddr(device.getDevice(), "vkGetRayTracingShaderGroupHandlesKHR"))
#define vkCmdTraceRaysKHR reinterpret_cast<PFN_vkCmdTraceRaysKHR>(vkGetDeviceProcAddr(device.getDevice(), "vkCmdTraceRaysKHR"))

#define MAX_DEPTH 10U

namespace RayTracing {
	struct StorageImage {
		VkImage image;
		VkDeviceMemory imageMemory;
		VkImageView imageView;
	};

	struct Uniform {
		glm::mat4 viewInverse;
		glm::mat4 projInverse;
		uint32_t frame;
		uint32_t depthMax;
	};

	class Pipeline {
	public:
		Pipeline(Core::Device& device, VkFormat format, VkExtent2D, AccelerationStructure topLevelAS, std::unique_ptr<Core::Buffer>& sceneInfoBuffer);
		~Pipeline();

		Pipeline(const Pipeline&) = delete;
		Pipeline operator=(Pipeline&) = delete;
		Pipeline(const Pipeline&&) = delete;
		Pipeline operator=(Pipeline&&) = delete;

		void bind(VkCommandBuffer buffer);
		void bindDescriptorSets(VkCommandBuffer buffer, uint32_t index);
		void traceRays(VkCommandBuffer buffer, uint32_t width, uint32_t height, uint32_t depth);
		void writeToUniformBuffer(void* data, uint32_t index);
		void rebuildRenderOutput(VkFormat format, VkExtent2D extent);
		void updateTopLevelAS(AccelerationStructure topLevelAS);

		inline StorageImage& getRenderOutput() { return storageImage; }

		static std::unique_ptr<Pipeline> createPipeline(Core::Device& device, std::unique_ptr<Core::SwapChain>& swapChain, Scene& scene);
	private:
		void createUniformBuffers();
		void createStorageImage();
		void createDescriptorSets();
		void createPipelineLayout();
		void createPipeline();
		void createShaderBindingTable(const VkRayTracingPipelineCreateInfoKHR& rtPipelineInfo);

		void readShader(std::string path, VkShaderModule* module);
		std::vector<char> readShaderFile(std::string& path);
		void createShaderModule(const std::vector<char>& code, VkShaderModule* module);

		void destroyStorageImage();
	private:
		Core::Device& device;

		VkFormat format;
		VkExtent2D extent;
		StorageImage storageImage;

		AccelerationStructure topLevelAS;
		std::unique_ptr<Core::Buffer>& sceneInfoBuffer;

		VkPipeline graphicsPipeline;
		VkPipelineLayout graphicsPipelineLayout;

		std::unique_ptr<Core::DescriptorPool> globalPool{};
		std::unique_ptr<Core::DescriptorSetLayout> globalSetLayout;
		std::vector<VkDescriptorSet> globalDescriptorSets;
		std::vector<std::unique_ptr<Core::Buffer>> uniformBuffers;

		std::unique_ptr<Core::Buffer> sbtBuffer;
		std::vector<uint8_t> shaderHandles;
		std::vector<char> shaderRawCode;
		VkShaderModule rtShaderModule;

		VkStridedDeviceAddressRegionKHR raygenRegion{};
		VkStridedDeviceAddressRegionKHR missRegion{};
		VkStridedDeviceAddressRegionKHR hitRegion{};
		VkStridedDeviceAddressRegionKHR callableRegion{};
	};
}