#include "RTApp.h"

RayTracing::RTApp::RTApp(std::string title, int width, int height) : window({ width, height, title }), device(&window), scene(device) {
	createPipeline();

	scene.loadAndAppendModel("");
}

RayTracing::RTApp::~RTApp() {
	destroyStorageImage();
	freeCommandBuffers();

	vkDestroyPipeline(device.getDevice(), graphicsPipeline, nullptr);
	vkDestroyPipelineLayout(device.getDevice(), graphicsPipelineLayout, nullptr);
	vkDestroyShaderModule(device.getDevice(), rtShaderModule, nullptr);
}

void RayTracing::RTApp::createPipeline() {
	BUILD_DEBUG("Creating Ray Tracing Pipeline", 0, 4, "Creating Storage Image...");
	createStorageImage();
	BUILD_DEBUG("Creating Ray Tracing Pipeline", 1, 4, "Creating Descriptor Sets...");
	createRayTracingDescriptorSets();
	BUILD_DEBUG("Creating Ray Tracing Pipeline", 2, 4, "Creating Pipeline Layout...");
	createRayTracingPipelineLayout();
	BUILD_DEBUG("Creating Ray Tracing Pipeline", 3, 4, "Creating Ray Tracing Pipeline...");
	createRayTracingPipeline();
	BUILD_DEBUG("Creating Ray Tracing Pipeline", 4, 4, "Creation completed!");

	DEBUG("Creating Command Buffers");
	createCommandBuffers();
	DEBUG("Command Buffers created!");
}

void RayTracing::RTApp::createRayTracingPipelineLayout() {

	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 1;
	VkDescriptorSetLayout layout = globalSetLayout->getDescriptorSetLayout();
	pipelineLayoutInfo.pSetLayouts = &layout;

	VK_CHECK_RESULT(vkCreatePipelineLayout(device.getDevice(), &pipelineLayoutInfo, nullptr, &graphicsPipelineLayout), "failed to create pipeline layout");
}

void RayTracing::RTApp::createRayTracingDescriptorSets() {
	globalPool = Core::DescriptorPool::Builder(device)
		.setMaxSets(Core::SwapChain::MAX_FRAMES_IN_FLIGHT)
		.addPoolSize(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, Core::SwapChain::MAX_FRAMES_IN_FLIGHT)
		.addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, Core::SwapChain::MAX_FRAMES_IN_FLIGHT)
		.addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, Core::SwapChain::MAX_FRAMES_IN_FLIGHT)
		.build();

	uniformBuffers.resize(Core::SwapChain::MAX_FRAMES_IN_FLIGHT);
	createUniformBuffers();

	globalSetLayout = Core::DescriptorSetLayout::Builder(device)
		.addBinding(0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_SHADER_STAGE_ALL, 1)
		.addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_ALL, 1)
		.addBinding(2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL, 1)
		.build();

	globalDescriptorSets.resize(Core::SwapChain::MAX_FRAMES_IN_FLIGHT);

	VkDescriptorImageInfo imageInfo{};
	imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	imageInfo.imageView = storageImage.imageView;

	VkWriteDescriptorSetAccelerationStructureKHR accelInfo{};
	accelInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
	accelInfo.accelerationStructureCount = 1;
	accelInfo.pAccelerationStructures = &scene.getTlas().handle;

	for (int i = 0; i < Core::SwapChain::MAX_FRAMES_IN_FLIGHT; i++) {
		auto uboBufInfo = uniformBuffers[i]->descriptorInfo();

		Core::DescriptorWriter(*globalSetLayout, *globalPool)
			.writeAccelStructure(0, &accelInfo)
			.writeImage(1, &imageInfo)
			.writeBuffer(2, &uboBufInfo)
			.build(globalDescriptorSets[i]);
	}
}

void RayTracing::RTApp::createStorageImage() {
	VK_FORMAT_R32G32B32A32_SFLOAT;

	VkImageCreateInfo imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.format = swapChain->getSwapChainImageFormat();
	imageInfo.extent.height = swapChain->getSwapChainExtent().height;
	imageInfo.extent.width = swapChain->getSwapChainExtent().width;

	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VkImageViewCreateInfo viewInfo{
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = swapChain->getSwapChainImageFormat(),
		.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1 },
	};

	device.createImageWithInfo(imageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, storageImage.image, storageImage.imageMemory);

	viewInfo.image = storageImage.image;

	VK_CHECK_RESULT(vkCreateImageView(device.getDevice(), &viewInfo, nullptr, &storageImage.imageView), "failed to create texture image view!");
}

void RayTracing::RTApp::createRayTracingPipeline() {
	enum StageIndices {
		eRayGen,
		eMiss,
		eClosestHit,
		eShaderGroupCount
	};

	std::array<VkPipelineShaderStageCreateInfo, eShaderGroupCount> stages{};

	for (VkPipelineShaderStageCreateInfo& info : stages)
		info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;

	readShader("shaders/raytracing.slang.spv", &rtShaderModule);
	//stages[eRayGen].pNext = &shaderCode;
	stages[eRayGen].pName = "rgenMain";
	stages[eRayGen].stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
	stages[eRayGen].module = rtShaderModule;

	//stages[eMiss].pNext = &shaderCode;
	stages[eMiss].pName = "rmissMain";
	stages[eMiss].stage = VK_SHADER_STAGE_MISS_BIT_KHR;
	stages[eMiss].module = rtShaderModule;

	//stages[eClosestHit].pNext = &shaderCode;
	stages[eClosestHit].pName = "rchitMain";
	stages[eClosestHit].stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
	stages[eClosestHit].module = rtShaderModule;

	std::vector<VkRayTracingShaderGroupCreateInfoKHR> shader_groups;

	VkRayTracingShaderGroupCreateInfoKHR group{ VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR };
	group.anyHitShader = VK_SHADER_UNUSED_KHR;
	group.closestHitShader = VK_SHADER_UNUSED_KHR;
	group.generalShader = VK_SHADER_UNUSED_KHR;
	group.intersectionShader = VK_SHADER_UNUSED_KHR;

	group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
	group.generalShader = eRayGen;
	shader_groups.push_back(group);

	group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
	group.generalShader = eMiss;
	shader_groups.push_back(group);

	group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
	group.generalShader = VK_SHADER_UNUSED_KHR;
	group.closestHitShader = eClosestHit;
	shader_groups.push_back(group);

	//layout is already created

	VkRayTracingPipelineCreateInfoKHR rtPipelineInfo{ VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR };
	rtPipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
	rtPipelineInfo.pStages = stages.data();
	rtPipelineInfo.groupCount = static_cast<uint32_t>(shader_groups.size());
	rtPipelineInfo.pGroups = shader_groups.data();
	rtPipelineInfo.maxPipelineRayRecursionDepth = std::max(3U, device.getRTProperties()->maxRayRecursionDepth);
	rtPipelineInfo.layout = graphicsPipelineLayout;

	vkCreateRayTracingPipelinesKHR(device.getDevice(), {}, {}, 1, & rtPipelineInfo, nullptr, & graphicsPipeline);

	std::cout << "Creating Shader Binding Table..." << std::endl;
	createShaderBindingTable(rtPipelineInfo);
	std::cout << "Shader Binding Table created" << std::endl;
}

void RayTracing::RTApp::createShaderBindingTable(const VkRayTracingPipelineCreateInfoKHR& rtPipelineInfo) {

	uint32_t handleSize = device.getRTProperties()->shaderGroupHandleSize;
	uint32_t handleAlignment = device.getRTProperties()->shaderGroupHandleAlignment;
	uint32_t baseAlignment = device.getRTProperties()->shaderGroupBaseAlignment;
	uint32_t groupCount = rtPipelineInfo.groupCount;

	size_t dataSize = handleSize * groupCount;
	shaderHandles.resize(dataSize);
	VK_CHECK_RESULT(vkGetRayTracingShaderGroupHandlesKHR(device.getDevice(), graphicsPipeline, 0, groupCount, dataSize, shaderHandles.data()), "failed to get shader shader handles!");

	auto     alignUp = [](uint32_t size, uint32_t alignment) { return (size + alignment - 1) & ~(alignment - 1); };
	uint32_t raygenSize = alignUp(handleSize, handleAlignment);
	uint32_t missSize = alignUp(handleSize, handleAlignment);
	uint32_t hitSize = alignUp(handleSize, handleAlignment);
	uint32_t callableSize = 0; //unused

	uint32_t raygenOffset = 0;
	uint32_t missOffset = alignUp(raygenSize, baseAlignment);
	uint32_t hitOffset = alignUp(missOffset + missSize, baseAlignment);
	uint32_t callableOffset = alignUp(hitOffset + hitSize, baseAlignment);

	size_t bufferSize = callableOffset + callableSize;

	sbtBuffer = std::make_unique<Core::Buffer>(
		device,
		bufferSize,
		VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
	);

	sbtBuffer->map();
	uint8_t* pData = static_cast<uint8_t*>(sbtBuffer->getMappedMemory());
	memcpy(pData + raygenOffset, shaderHandles.data() + 0 * handleSize, handleSize);
	raygenRegion.deviceAddress = sbtBuffer->getAddress() + raygenOffset;
	raygenRegion.size = raygenSize;
	raygenRegion.stride = raygenSize;

	memcpy(pData + missOffset, shaderHandles.data() + 1 * handleSize, handleSize);
	missRegion.deviceAddress = sbtBuffer->getAddress() + missOffset;
	missRegion.size = missSize;
	missRegion.stride = missSize;

	memcpy(pData + hitOffset, shaderHandles.data() + 2 * handleSize, handleSize);
	hitRegion.deviceAddress = sbtBuffer->getAddress() + hitOffset;
	hitRegion.size = hitSize;
	hitRegion.stride = hitSize;

	callableRegion.deviceAddress = 0;
	callableRegion.size = 0;
	callableRegion.stride = 0;
}

void RayTracing::RTApp::createUniformBuffers() {
	for (uint32_t i = 0; i < uniformBuffers.size(); i++) {
		uniformBuffers[i] = std::make_unique<Core::Buffer>(
			device,
			sizeof(Uniform),
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
		);

		uniformBuffers[i]->map();
	}
}

void RayTracing::RTApp::readShader(std::string file, VkShaderModule* module) {
	std::vector<char> code = readShaderFile(file);

	createShaderModule(code, module);
}

std::vector<char> RayTracing::RTApp::readShaderFile(std::string& path) {
	std::ifstream file(path, std::ios::ate | std::ios::binary);

	if (!file.is_open()) throw std::runtime_error("failed to open: " + path);

	size_t size = static_cast<size_t>(file.tellg());
	std::vector<char> buf(size);
	file.seekg(0);
	file.read(buf.data(), size);
	file.close();

	return buf;
}

void RayTracing::RTApp::createShaderModule(const std::vector<char>& code, VkShaderModule* module) {
	VkShaderModuleCreateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	info.codeSize = code.size();
	info.pCode = reinterpret_cast<const uint32_t*>(code.data());

	VK_CHECK_RESULT(vkCreateShaderModule(device.getDevice(), &info, nullptr, module), "failed to create Shader module");
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

void RayTracing::RTApp::recreateSwapChain() {
	auto extent = window.getExtent();

	while (extent.width == 0 || extent.height == 0) {
		extent = window.getExtent();
		glfwWaitEvents();
	}

	vkDeviceWaitIdle(device.getDevice());

	if (swapChain == nullptr)
		swapChain = std::make_unique<Core::SwapChain>(device, extent);
	else {
		std::shared_ptr<Core::SwapChain> oldSwapChain = std::move(swapChain);
		swapChain = std::make_unique<Core::SwapChain>(device, extent, oldSwapChain);

		if (!oldSwapChain->compareSwapFormats(*swapChain.get())) throw std::runtime_error("Swap Chain image format has changed!");
	}
}

// -------------------- RENDER FUNCTIONS --------------------

VkCommandBuffer RayTracing::RTApp::beginFrame() {
	assert(!frameStarted && "Can't call beginFrame while already in progress!");

	auto result = swapChain->acquireNextImage(&currentImageIndex);
	if (result == VK_ERROR_OUT_OF_DATE_KHR) {
		std::cout << "out of date" << std::endl;

		recreateSwapChain();
		return nullptr;
	}

	if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) throw std::runtime_error("failed to acquire swap chain image");

	frameStarted = true;
	auto commandBuffer = commandBuffers[currentFrameIndex];
	VkCommandBufferBeginInfo beginInfo{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
	};

	VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &beginInfo), "failed to begin command buffer!");

	return commandBuffer;
}

void RayTracing::RTApp::endFrame() {
	assert(frameStarted && "Can't call endFrame while frame is not in progress");

	//end command buffer
	auto commandBuffer = commandBuffers[currentFrameIndex];
	VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer), "failed to record buffer!");

	//submit command buffers
	auto result = swapChain->submitCommandBuffers(&commandBuffer, &currentImageIndex);
	//check results of the rendering and recreate the swap chain if the window has changed it's size
	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
		recreateSwapChain();
	}
	else VK_CHECK_RESULT(result, "failed to present swap chain image");

	//reset frameStarted and updated frame index
	frameStarted = false;
	currentFrameIndex = (currentFrameIndex + 1) % Core::SwapChain::MAX_FRAMES_IN_FLIGHT;
}

void RayTracing::RTApp::copyImageToSwapChain(VkCommandBuffer buffer, VkImage swapChainImage, VkImage storageImage, VkExtent2D size) {

	//this will call the next subpass
	//vkCmdNextSubpass(buffer, {subpassInformation});

	VkImageSubresourceRange ressourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

	{
		//Transition storage image
		VkImageMemoryBarrier srcBarrier{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			.image = storageImage,
			.subresourceRange = ressourceRange
		};
		vkCmdPipelineBarrier(buffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, VK_NULL_HANDLE, 0, VK_NULL_HANDLE, 1, &srcBarrier);
	}

	{
		//Transition swap chain image
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

	//copy storage image to swap chain
	VkImageCopy region{
		.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
			.srcOffset = {0, 0, 0},
			.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
			.dstOffset = {0, 0, 0},
			.extent = {size.width, size.height, 1}
	};

	vkCmdCopyImage(buffer, storageImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapChainImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	{
		//transition swap chain image
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
		//transition storage image
		VkImageMemoryBarrier srcBarrier{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
			.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_GENERAL,
			.image = storageImage,
			.subresourceRange = ressourceRange
		};

		vkCmdPipelineBarrier(buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, VK_NULL_HANDLE, 0, VK_NULL_HANDLE, 1, &srcBarrier);
	}
}

void RayTracing::RTApp::rayTraceScene() {
	//start recording of the command buffer
	if (auto buffer = beginFrame()) {
		vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, graphicsPipeline);

		{
			//Convert storage image to general layout
			VkImageMemoryBarrier toGeneral{
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.srcAccessMask = 0,
				.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.newLayout = VK_IMAGE_LAYOUT_GENERAL,
				.image = storageImage.image,
				.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
			};

			vkCmdPipelineBarrier(buffer, 0, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 0, 0, VK_NULL_HANDLE, 0, VK_NULL_HANDLE, 1, &toGeneral);
		}

		vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, graphicsPipelineLayout, 0, 1, &globalDescriptorSets[currentFrameIndex], 0, nullptr);

		//Update uniform buffers and push changes to the gpu
		Uniform info{};
		info.viewInverse = glm::inverse(glm::transpose(camera.getView()));
		info.projInverse = camera.getProjection(); //projection matrix is already inversed

		uniformBuffers[currentFrameIndex]->writeToBuffer(&info);
		uniformBuffers[currentFrameIndex]->flush();

		//Call the ray tracing command
		VkExtent2D size = swapChain->getSwapChainExtent();
		vkCmdTraceRaysKHR(buffer, &raygenRegion, &missRegion, &hitRegion, &callableRegion, size.width, size.height, 1);

		//The rendered image will later be processed by the denoiser an then copied to the swap chain

		//copy rendered image to swap chain for presentation
		copyImageToSwapChain(buffer, swapChain->getImage(currentImageIndex), storageImage.image, size);

		endFrame();

	}
}

void RayTracing::RTApp::destroyStorageImage() {
	vkDestroyImageView(device.getDevice(), storageImage.imageView, nullptr);
	vkDestroyImage(device.getDevice(), storageImage.image, nullptr);
	vkFreeMemory(device.getDevice(), storageImage.imageMemory, nullptr);
}

void RayTracing::RTApp::freeCommandBuffers() {
	vkFreeCommandBuffers(device.getDevice(), device.getCommandPool(), Core::SwapChain::MAX_FRAMES_IN_FLIGHT, commandBuffers.data());
}