#pragma once

#include <chrono>
#include "../Window.h"
#include "../Camera.h"
#include "../vulkan_core/Device.h"
#include "../vulkan_core/SwapChain.h"

#include "Scene.h"
#include "RTPipeline.h"

namespace RayTracing {
	class RTApp {
	public:
		RTApp();
		~RTApp();

		void run();
	private:
		void createCommandBuffers();
		void prepareStorageImage(VkCommandBuffer buffer);
		void copyImageToSwapchain(VkCommandBuffer buffer, VkImage swapChainImage, VkExtent2D size);
		void rayTraceScene();
		VkCommandBuffer beginFrame();
		void endFrame();

		void recreateSwapChain();

	private:
		Core::Window window;
		Core::Device device;
		Core::Camera camera;
		Scene scene;
		std::unique_ptr<Core::SwapChain> swapChain;
		std::unique_ptr<Pipeline> rtPipeline;

		std::vector<VkCommandBuffer> commandBuffers;

		bool frameStarted;
		bool discardFrame;
		uint32_t frameIndex;
		uint32_t imageIndex;
	};
}