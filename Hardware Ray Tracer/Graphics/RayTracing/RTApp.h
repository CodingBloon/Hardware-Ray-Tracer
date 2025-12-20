#pragma once
#include <string>
#include <array>
#include <fstream>
#include "../Window.h"
#include "../vulkan_core/Device.h"
#include "../vulkan_core/Buffer.h"
#include "../vulkan_core/SwapChain.h"
#include "../vulkan_core/Descriptors.h"
#include "../Camera.h"
#include "Scene.h"

#define DEBUG(message) std::cout << message << std::endl
#define BUILD_DEBUG(message, currentStage, totalStages, createMessage) std::cout << message << " " << currentStage << " of " << totalStages << " completed! " << createMessage << std::endl

namespace RayTracing {

	struct StorageImage {
		VkImage image;
		VkDeviceMemory imageMemory;
		VkImageView imageView;
	};

	struct Uniform {
		glm::mat4 viewInverse;
		glm::mat4 projInverse;
	};

	class RTApp {
	public:
		RTApp(std::string title, int width, int height);
		~RTApp();

		void run();
	private:
		// -------------------- PIPELINE CREATION --------------------
		void createPipeline();

		void createRayTracingPipelineLayout();
		void createRayTracingDescriptorSets();
		void createStorageImage();
		void createRayTracingPipeline();
		void createShaderBindingTable(const VkRayTracingPipelineCreateInfoKHR& rtPipelineInfo);

		// -------------------- RENDER FUNCTIONS --------------------
		void createCommandBuffers();
		void recreateSwapChain();

		VkCommandBuffer beginFrame();
		void endFrame();
		void copyImageToSwapChain(VkCommandBuffer buffer, VkImage swapChainImage, VkImage storageImage, VkExtent2D size);
		void rayTraceScene();

		//-------------------- SHADER CREATION --------------------
		void readShader(std::string file, VkShaderModule* module);
		std::vector<char> readShaderFile(std::string& file);
		void createShaderModule(const std::vector<char>& code, VkShaderModule* module);

		//-------------------- BUFFER CREATION --------------------
		void createUniformBuffers();

		// -------------------- DESTROY FUNCTIONS --------------------
		void destroyStorageImage();
		void destroyAccelerationStructures();
		void freeCommandBuffers();
	private:
		Core::Window window;
		Core::Device device;
		std::unique_ptr<Core::SwapChain> swapChain;
		Core::Camera camera;

		Scene scene;
		StorageImage storageImage;

		std::unique_ptr<Core::DescriptorPool> globalPool{};
		std::unique_ptr<Core::DescriptorSetLayout> globalSetLayout;
		std::vector<VkDescriptorSet> globalDescriptorSets;
		std::vector<std::unique_ptr<Core::Buffer>> uniformBuffers;

		VkPipelineLayout graphicsPipelineLayout;
		VkPipeline graphicsPipeline;

		std::unique_ptr<Core::Buffer> sbtBuffer;
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