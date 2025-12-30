#include "RTPipeline.h"
#include "Debugging.h"

RayTracing::Pipeline::Pipeline(Core::Device& device, VkFormat format, VkExtent2D extent, AccelerationStructure topLevelAS, std::unique_ptr<Core::Buffer>& sceneInfoBuffer) 
	: device(device), 
	format(format), 
	extent(extent), 
	topLevelAS(topLevelAS),
	sceneInfoBuffer(sceneInfoBuffer) {
	
	BUILD("Ray Tracing Pipeline", 0, 5, "Creating uniform buffers...");
	uniformBuffers.resize(Core::SwapChain::MAX_FRAMES_IN_FLIGHT);
	createUniformBuffers();

	BUILD("Ray Tracing Pipeline", 1, 5, "Creating Storage Image...");
	createStorageImage();
	
	BUILD("Ray Tracing Pipeline", 2, 5, "Creating Pipeline Descriptor Sets...");
	createDescriptorSets();
	BUILD("Ray Tracing Pipeline", 3, 5, "Creating Pipeline Layout...");
	createPipelineLayout();
	BUILD("Ray Tracing Pipeline", 4, 5, "Creating Pipeline...");
	createPipeline();

	BUILD("Ray Tracing Pipeline", 5, 5, "Pipeline created!");
}
RayTracing::Pipeline::~Pipeline() {
	destroyStorageImage();

	vkDestroyPipeline(device.getDevice(), graphicsPipeline, nullptr);
	vkDestroyPipelineLayout(device.getDevice(), graphicsPipelineLayout, nullptr);
	vkDestroyShaderModule(device.getDevice(), rtShaderModule, nullptr);
}

void RayTracing::Pipeline::bind(VkCommandBuffer buffer) {
	vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, graphicsPipeline);
}
void RayTracing::Pipeline::bindDescriptorSets(VkCommandBuffer buffer, uint32_t index) {
	vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, graphicsPipelineLayout, 0, 1, &globalDescriptorSets[index], 0, VK_NULL_HANDLE);
}
void RayTracing::Pipeline::traceRays(VkCommandBuffer buffer, uint32_t width, uint32_t height, uint32_t depth) {
	vkCmdTraceRaysKHR(buffer, &raygenRegion, &missRegion, &hitRegion, &callableRegion, width, height, depth);
}
void RayTracing::Pipeline::writeToUniformBuffer(void* data, uint32_t index) {
	uniformBuffers[index]->writeToBuffer(data);
	uniformBuffers[index]->flush();
}

void RayTracing::Pipeline::rebuildRenderOutput(VkFormat format, VkExtent2D extent) {
	destroyStorageImage();
	this->format = format;
	this->extent = extent;
	createStorageImage();
	createDescriptorSets();
}

void RayTracing::Pipeline::updateTopLevelAS(AccelerationStructure topLevelAS) {
	
}

void RayTracing::Pipeline::createUniformBuffers() {
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

void RayTracing::Pipeline::createStorageImage() {
	VkImageCreateInfo imageInfo{
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = format,
		.extent = {.width = extent.width, .height = extent.height, .depth = 1},
		.mipLevels = 1,
		.arrayLayers = 1, 
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
	};

	VkImageViewCreateInfo viewInfo{
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = format,
		.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1 },
	};

	device.createImageWithInfo(imageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, storageImage.image, storageImage.imageMemory);

	viewInfo.image = storageImage.image;

	VK_CHECK_RESULT(vkCreateImageView(device.getDevice(), &viewInfo, nullptr, &storageImage.imageView), "failed to create texture image view!");
}
void RayTracing::Pipeline::createDescriptorSets() {
	globalPool = Core::DescriptorPool::Builder(device)
		.setMaxSets(Core::SwapChain::MAX_FRAMES_IN_FLIGHT)
		.addPoolSize(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, Core::SwapChain::MAX_FRAMES_IN_FLIGHT)
		.addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, Core::SwapChain::MAX_FRAMES_IN_FLIGHT)
		.addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, Core::SwapChain::MAX_FRAMES_IN_FLIGHT)
		.addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, Core::SwapChain::MAX_FRAMES_IN_FLIGHT)
		.build();

	globalSetLayout = Core::DescriptorSetLayout::Builder(device)
		.addBinding(0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_SHADER_STAGE_ALL, 1)
		.addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_ALL, 1)
		.addBinding(2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL, 1)
		.addBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_ALL, 1)
		.build();

	globalDescriptorSets.resize(Core::SwapChain::MAX_FRAMES_IN_FLIGHT);

	VkDescriptorImageInfo imageInfo{};
	imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	imageInfo.imageView = storageImage.imageView;

	VkWriteDescriptorSetAccelerationStructureKHR accelInfo{};
	accelInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
	accelInfo.accelerationStructureCount = 1;
	accelInfo.pAccelerationStructures = &topLevelAS.handle;

	auto sceneInfo = sceneInfoBuffer->descriptorInfo();

	for (int i = 0; i < Core::SwapChain::MAX_FRAMES_IN_FLIGHT; i++) {
		auto uboBufInfo = uniformBuffers[i]->descriptorInfo();

		Core::DescriptorWriter(*globalSetLayout, *globalPool)
			.writeAccelStructure(0, &accelInfo)
			.writeImage(1, &imageInfo)
			.writeBuffer(2, &uboBufInfo)
			.writeBuffer(3, &sceneInfo)
			.build(globalDescriptorSets[i]);
	}
}

void RayTracing::Pipeline::createPipelineLayout() {
	VkDescriptorSetLayout layout = globalSetLayout->getDescriptorSetLayout();

	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
	.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
	.setLayoutCount = 1,
	.pSetLayouts = &layout
	};

	VK_CHECK_RESULT(vkCreatePipelineLayout(device.getDevice(), &pipelineLayoutInfo, nullptr, &graphicsPipelineLayout), "failed to create pipeline layout");
}

void RayTracing::Pipeline::createPipeline() {
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
	stages[eRayGen].pName = "rgenMain";
	stages[eRayGen].stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
	stages[eRayGen].module = rtShaderModule;

	stages[eMiss].pName = "rmissMain";
	stages[eMiss].stage = VK_SHADER_STAGE_MISS_BIT_KHR;
	stages[eMiss].module = rtShaderModule;

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

	vkCreateRayTracingPipelinesKHR(device.getDevice(), {}, {}, 1, &rtPipelineInfo, nullptr, &graphicsPipeline);

	std::cout << "Creating Shader Binding Table..." << std::endl;
	createShaderBindingTable(rtPipelineInfo);
	std::cout << "Shader Binding Table created" << std::endl;
}

void RayTracing::Pipeline::createShaderBindingTable(const VkRayTracingPipelineCreateInfoKHR& rtPipelineInfo) {
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

void RayTracing::Pipeline::readShader(std::string path, VkShaderModule* module) {
	std::vector<char> code = readShaderFile(path);
	createShaderModule(code, module);
}

std::vector<char> RayTracing::Pipeline::readShaderFile(std::string& path) {
	std::ifstream file(path, std::ios::ate | std::ios::binary);

	if (!file.is_open()) throw std::runtime_error("failed to open: " + path);

	size_t size = static_cast<size_t>(file.tellg());
	std::vector<char> buf(size);
	file.seekg(0);
	file.read(buf.data(), size);
	file.close();

	return buf;
}

void RayTracing::Pipeline::createShaderModule(const std::vector<char>& code, VkShaderModule* module) {
	VkShaderModuleCreateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	info.codeSize = code.size();
	info.pCode = reinterpret_cast<const uint32_t*>(code.data());

	VK_CHECK_RESULT(vkCreateShaderModule(device.getDevice(), &info, nullptr, module), "failed to create Shader module");
}

void RayTracing::Pipeline::destroyStorageImage() {
	vkDestroyImageView(device.getDevice(), storageImage.imageView, nullptr);
	vkDestroyImage(device.getDevice(), storageImage.image, nullptr);
	vkFreeMemory(device.getDevice(), storageImage.imageMemory, nullptr);
}

std::unique_ptr<RayTracing::Pipeline> RayTracing::Pipeline::createPipeline(Core::Device& device, std::unique_ptr<Core::SwapChain>& swapChain, Scene& scene) {
	return std::make_unique<RayTracing::Pipeline>(
		device,
		swapChain->getSwapChainImageFormat(),
		swapChain->getSwapChainExtent(),
		scene.getTlas(),
		scene.getSceneInfoBuffer()
	);
}