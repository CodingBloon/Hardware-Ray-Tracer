#include "RTApp.h"

RayTracing::RTApp::RTApp() : window({800, 600, "Ray Tracer | DLSS 4"}), device(&window), scene(device) {
	scene.loadModel("models/Cube.obj");
	scene.build();

	recreateSwapChain();
	rtPipeline = Pipeline::createPipeline(device, swapChain, scene);
	
	BUILD("Command Buffer Build", 0, 1, "Creating command buffers...");
	createCommandBuffers();
	BUILD("Command Buffer Build", 1, 1, "Command buffers created!");

	camera.setView(glm::vec3(0.0f, 0.0f, -2.0f), glm::vec3());
}
RayTracing::RTApp::~RTApp() {}

void RayTracing::RTApp::run() {
	auto currentTime = std::chrono::high_resolution_clock::now();

	while (!window.shouldClose()) {
		glfwPollEvents();

		auto newTime = std::chrono::high_resolution_clock::now();
		float delta = std::chrono::duration<float, std::chrono::seconds::period>(newTime - currentTime).count();
		currentTime = newTime;

		camera.handleInputs(window.getGLFWWindow(), delta);
		float aspectRatio = swapChain->extentAspectRatio();
		camera.setPerspectiveProjection(glm::radians(60.f), aspectRatio, 0.001f, 100000.f);

		//update uniform buffer
		Uniform uniform{
			.viewInverse = glm::inverse(glm::transpose(camera.getView())),
			.projInverse = glm::inverse(glm::transpose(camera.getProjection()))
		};

		rtPipeline->writeToUniformBuffer(&uniform, frameIndex);

		//render scene
		rayTraceScene();

	}

	vkDeviceWaitIdle(device.getDevice());
}

void RayTracing::RTApp::createCommandBuffers() {
	commandBuffers.resize(Core::SwapChain::MAX_FRAMES_IN_FLIGHT);

	VkCommandBufferAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandPool = device.getCommandPool();
	allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

	VK_CHECK_RESULT(vkAllocateCommandBuffers(device.getDevice(), &allocInfo, commandBuffers.data()), "failed to allocate command buffers");
}

void RayTracing::RTApp::prepareStorageImage(VkCommandBuffer buffer) {
	VkImageMemoryBarrier barrier{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout = VK_IMAGE_LAYOUT_GENERAL,
		.image = rtPipeline->getRenderOutput().image,
		.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
	};

	vkCmdPipelineBarrier(buffer, 0, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 0, 0, VK_NULL_HANDLE, 0, VK_NULL_HANDLE, 1, &barrier);
}

void RayTracing::RTApp::copyImageToSwapchain(VkCommandBuffer buffer, VkImage swapChainImage, VkExtent2D size) {
	VkImageSubresourceRange ressourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	
	{
		VkImageMemoryBarrier srcBarrier{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			.image = rtPipeline->getRenderOutput().image,
			.subresourceRange = ressourceRange
		};
		vkCmdPipelineBarrier(buffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, VK_NULL_HANDLE, 0, VK_NULL_HANDLE, 1, &srcBarrier);
	}
	{
		VkImageMemoryBarrier dstBarrier{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = 0,
			.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.image = swapChainImage,
			.subresourceRange = ressourceRange
		};

		vkCmdPipelineBarrier(buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, VK_NULL_HANDLE, 0, VK_NULL_HANDLE, 1, &dstBarrier);
	}

	VkImageCopy region{
		.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
			.srcOffset = {0, 0, 0},
			.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
			.dstOffset = {0, 0, 0},
			.extent = {size.width, size.height, 1}
	};

	if (!discardFrame)
		vkCmdCopyImage(buffer, rtPipeline->getRenderOutput().image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapChainImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	{
		VkImageMemoryBarrier dstBarrier{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask = 0,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			.image = swapChainImage,
			.subresourceRange = ressourceRange
		};
		vkCmdPipelineBarrier(buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, VK_NULL_HANDLE, 0, VK_NULL_HANDLE, 1, &dstBarrier);
	}
	{
		VkImageMemoryBarrier srcBarrier{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
			.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_GENERAL,
			.image = rtPipeline->getRenderOutput().image,
			.subresourceRange = ressourceRange
		};

		vkCmdPipelineBarrier(buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, VK_NULL_HANDLE, 0, VK_NULL_HANDLE, 1, &srcBarrier);
	}
}
void RayTracing::RTApp::rayTraceScene() {
	if (auto buffer = beginFrame()) {
		rtPipeline->bind(buffer);
		prepareStorageImage(buffer);
		rtPipeline->bindDescriptorSets(buffer, frameIndex);

		VkExtent2D size = swapChain->getSwapChainExtent();
		rtPipeline->traceRays(buffer, size.width, size.height, 1);

		//DLSS Ray Reconstruction will denoise the image
		//DLSS Super Resolution will upscale the image
		copyImageToSwapchain(buffer, swapChain->getImage(imageIndex), swapChain->getSwapChainExtent());

		endFrame();
	}

	discardFrame = false;
}
VkCommandBuffer RayTracing::RTApp::beginFrame() {
	assert(!frameStarted);

	auto result = swapChain->acquireNextImage(&imageIndex);
	if (result == VK_ERROR_OUT_OF_DATE_KHR) {
		recreateSwapChain();
		return nullptr;
	}

	if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) throw std::runtime_error("failed to acquire swap chain image");
	frameStarted = true;
	auto commandBuffer = commandBuffers[frameIndex];
	VkCommandBufferBeginInfo beginInfo{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
	};

	VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &beginInfo), "failed to begin command buffer!");

	return commandBuffer;
}
void RayTracing::RTApp::endFrame() {
	assert(frameStarted);

	//end command buffer
	auto commandBuffer = commandBuffers[frameIndex];
	VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer), "failed to record buffer!");

	//submit command buffers
	auto result = swapChain->submitCommandBuffers(&commandBuffer, &imageIndex);
	//check results of the rendering and recreate the swap chain if the window has changed it's size
	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || window.wasWindowResized()) {
		window.resetWindowResizeFlag();
		discardFrame = true;
		recreateSwapChain();
		rtPipeline->rebuildRenderOutput(swapChain->getSwapChainImageFormat(), swapChain->getSwapChainExtent());
	}
	else VK_CHECK_RESULT(result, "failed to present swap chain image");

	//reset frameStarted and updated frame index
	frameStarted = false;
	frameIndex = (frameIndex + 1) % Core::SwapChain::MAX_FRAMES_IN_FLIGHT;
}

void RayTracing::RTApp::recreateSwapChain() {
	auto extent = window.getExtent();

	while (extent.width == 0 || extent.height == 0) {
		extent = window.getExtent();
		glfwWaitEvents();
	}

	vkDeviceWaitIdle(device.getDevice());

	if (swapChain == nullptr)
		swapChain = std::make_unique<Core::SwapChain>(device, window.getExtent());
	else {
		std::shared_ptr<Core::SwapChain> oldSwapChain = std::move(swapChain);
		swapChain = std::make_unique<Core::SwapChain>(device, extent, oldSwapChain);

		if (!swapChain->compareSwapFormats(*oldSwapChain.get())) throw std::runtime_error("Swap chain format changed!");
	}
}