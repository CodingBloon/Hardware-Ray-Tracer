#include "App.h"
#include <ctime>
#define TINYOBJLOADER_IMPLEMENTATION
#include "../tinyobj/tiny_obj_loader.h"

/*
 * Der Hardware Ray Tracer nutzt die dedizierte Ray-Tracing-Hardware moderner Grafikkarten. 
 * Wird eine Grafikkarte ohne hardwarebeschleunigte Ray-Tracing-Unterstützung verwendet, 
 * kann es dazu kommen, dass das Ray Tracing nicht initialisiert werden kann. 
 * In diesem Fall kann das Programm möglicherweise nicht starten, 
 * eine reduzierte Leistung aufweisen oder Fehlermeldungen ausgeben.
 * 
 * Hinweis: Der Hardware Ray Tracer erfordert eine Grafikkarte mit hardwarebeschleunigtem Ray Tracing. 
 * Ohne entsprechende Unterstützung kann es zu Startproblemen, Leistungseinbußen oder Fehlermeldungen kommen.
 * 
 * 
 * ALPHA-RELASE: Eingeschränkte Funktionen (Denoiser etc.)
 */

namespace std {
	template<>
	struct hash<Core::Vertex> {
		size_t operator()(Core::Vertex const& vert) const {
			size_t seed = 0;
			hashCombine(seed, vert.pos[0], vert.pos[1], vert.pos[2], 0);
			return seed;
		}
	};
}

Core::App::App() : window({800, 600, "Ray Tracing | DLSS 3.5"}), device(&window), swapChain(std::make_unique<SwapChain>(device, window.getExtent())) {

	loadModel("models/Monkey.obj");
	//generateMesh();

	createBottomLevelAS();
	createTopLevelAS();


	createStorageImage();
	createRayTracingDescriptorSets();
	createRayTracingPipelineLayout();
	createRayTracingPipeline();

	createCommandBuffers();

	camera.setView(glm::vec3(0.0f, 0.0f, -2.0f), glm::vec3());
}

Core::App::~App() {
	destroyAccelerationStructures();
	destroyStorageImage();

	freeCommandBuffers();

	vkDestroyPipeline(device.getDevice(), graphicsPipeline, nullptr);
	vkDestroyPipelineLayout(device.getDevice(), graphicsPipelineLayout, nullptr);
	vkDestroyShaderModule(device.getDevice(), rtShaderModule, nullptr);
}

void Core::App::run() {
	auto currentTime = std::chrono::high_resolution_clock::now();

	while (!window.shouldClose()) {
		glfwPollEvents();

		auto newTime = std::chrono::high_resolution_clock::now();
		float delta = std::chrono::duration<float, std::chrono::seconds::period>(newTime - currentTime).count();
		currentTime = newTime;

		camera.handleInputs(window.getGLFWWindow(), delta);
		float aspectRatio = swapChain->extentAspectRatio();
		camera.setPerspectiveProjection(glm::radians(60.f), aspectRatio, 0.001f, 100000.f);

		//update buffers

		//render scene
		rayTraceScene();
	}

	vkDeviceWaitIdle(device.getDevice());
}

// -------------------- RAY TRACING PIPELINE CREATION --------------------

void Core::App::createRayTracingPipelineLayout() {

	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 1;
	VkDescriptorSetLayout layout = globalSetLayout->getDescriptorSetLayout();
	pipelineLayoutInfo.pSetLayouts = &layout;

	VK_CHECK_RESULT(vkCreatePipelineLayout(device.getDevice(), &pipelineLayoutInfo, nullptr, &graphicsPipelineLayout), "failed to create pipeline layout");
}

void Core::App::createRayTracingDescriptorSets() {
	globalPool = DescriptorPool::Builder(device)
		.setMaxSets(SwapChain::MAX_FRAMES_IN_FLIGHT)
		.addPoolSize(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, SwapChain::MAX_FRAMES_IN_FLIGHT)
		.addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, SwapChain::MAX_FRAMES_IN_FLIGHT)
		.addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, SwapChain::MAX_FRAMES_IN_FLIGHT)
		.build();

	uniformBuffers.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
	createUniformBuffers();

	globalSetLayout = DescriptorSetLayout::Builder(device)
		.addBinding(0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_SHADER_STAGE_ALL, 1)
		.addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_ALL, 1)
		.addBinding(2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL, 1)
		.build();

	globalDescriptorSets.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);

	VkDescriptorImageInfo imageInfo{};
	imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	imageInfo.imageView = storageImage.imageView;

	VkWriteDescriptorSetAccelerationStructureKHR accelInfo{};
	accelInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
	accelInfo.accelerationStructureCount = 1;
	accelInfo.pAccelerationStructures = &tlasAccel.handle;

	for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; i++) {
		auto uboBufInfo = uniformBuffers[i]->descriptorInfo();

		DescriptorWriter(*globalSetLayout, *globalPool)
			.writeAccelStructure(0, &accelInfo)
			.writeImage(1, &imageInfo)
			.writeBuffer(2, &uboBufInfo)
			.build(globalDescriptorSets[i]);
	}
}

void Core::App::createStorageImage() {
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

void Core::App::createRayTracingPipeline() {
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

	vkCreateRayTracingPipelinesKHR(device.getDevice(), {}, {}, 1, & rtPipelineInfo, nullptr, & graphicsPipeline);

	std::cout << "Creating Shader Binding Table..." << std::endl;
	createShaderBindingTable(rtPipelineInfo);
	std::cout << "Shader Binding Table created" << std::endl;
}

void Core::App::createShaderBindingTable(const VkRayTracingPipelineCreateInfoKHR& rtPipelineInfo) {

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

	sbtBuffer = std::make_unique<Buffer>(
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

// -------------------- ACCELERATION STRUCTURE CREATION --------------------

void Core::App::primitiveToGeometry(const Mesh& mesh, VkAccelerationStructureGeometryKHR& geometry, VkAccelerationStructureBuildRangeInfoKHR& rangeInfo) {
	const auto triangeCount = static_cast<uint32_t>(mesh.indices.size() / 3U);

	std::cout << "Max Vertex: " << mesh.vertices.size() << std::endl;

	VkAccelerationStructureGeometryTrianglesDataKHR triangles{
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
		.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
		.vertexData = {.deviceAddress = mesh.vertexBuffer->getAddress()},
		.vertexStride = sizeof(Vertex),
		.maxVertex = static_cast<uint32_t>(mesh.vertices.size()) - 1,
		.indexType = VK_INDEX_TYPE_UINT32,
		.indexData = { .deviceAddress = mesh.indexBuffer->getAddress() },
	};

	geometry = VkAccelerationStructureGeometryKHR{
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
		.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
		.geometry = { .triangles = triangles },
		.flags = VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR | VK_GEOMETRY_OPAQUE_BIT_KHR,
	};

	rangeInfo = VkAccelerationStructureBuildRangeInfoKHR{ .primitiveCount = triangeCount };
}

void Core::App::createBottomLevelAS() {

	blasAccel.resize(meshes.size());

	std::cout << "Creating Accel Structures: " << blasAccel.size() << std::endl;

	for (uint32_t i = 0; i < meshes.size(); i++) {
		VkAccelerationStructureGeometryKHR asGeometry{};
		VkAccelerationStructureBuildRangeInfoKHR asBuildRangeInfo{};

		primitiveToGeometry(meshes[i], asGeometry, asBuildRangeInfo);

		createAccelerationStructure(VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, blasAccel[i], asGeometry, asBuildRangeInfo, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
	}
}

void Core::App::createTopLevelAS() {

	VkTransformMatrixKHR transformMatrix = {
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f };

	std::vector<VkAccelerationStructureInstanceKHR> tlasInstances;
	tlasInstances.reserve(blasAccel.size());

	for (uint32_t i = 0; i < blasAccel.size(); i++) {
		VkAccelerationStructureInstanceKHR asInstance{};
		asInstance.transform = transformMatrix;
		asInstance.instanceCustomIndex = i;
		asInstance.accelerationStructureReference = blasAccel[i].address;
		asInstance.instanceShaderBindingTableRecordOffset = 0;
		asInstance.mask = 0xFF;
		asInstance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_CULL_DISABLE_BIT_NV,
		tlasInstances.emplace_back(asInstance);
	}

	VkCommandBuffer cmd = device.beginSingleTimeCommands();

	constexpr size_t instanceAlignment = 16;

	Buffer stagingBuffer{
		device,
		std::span<VkAccelerationStructureInstanceKHR const>(tlasInstances).size_bytes(),
		VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		instanceAlignment
	};

	Buffer tlasInstanceBuffer{
		device,
		std::span<VkAccelerationStructureInstanceKHR const>(tlasInstances).size_bytes(),
		VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, 
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		instanceAlignment
	};

	stagingBuffer.map();
	stagingBuffer.writeToBuffer(tlasInstances.data());

	VkBufferCopy2 copyRegionInfo{
		.sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2,
		.srcOffset = 0,
		.dstOffset = 0,
		.size = std::span<VkAccelerationStructureInstanceKHR const>(tlasInstances).size_bytes()
	};



	VkCopyBufferInfo2 copyBufferInfo{
		.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
		.srcBuffer = stagingBuffer.getBuffer(),
		.dstBuffer = tlasInstanceBuffer.getBuffer(),
		.regionCount = 1,
		.pRegions = &copyRegionInfo
	};

	vkCmdCopyBuffer2(cmd, &copyBufferInfo);

	device.endSingleTimeCommands(cmd);
	
	{
		VkAccelerationStructureGeometryKHR asGeometry{};
		VkAccelerationStructureBuildRangeInfoKHR asBuildRangeInfo{};

		VkAccelerationStructureGeometryInstancesDataKHR geometryInstances{
				.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
				.data = {.deviceAddress = tlasInstanceBuffer.getAddress()}
		};

		asGeometry = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
			.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
			.geometry = { .instances = geometryInstances }
		};

		asBuildRangeInfo = { .primitiveCount = static_cast<uint32_t>(meshes.size())};

		createAccelerationStructure(VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, tlasAccel, asGeometry, asBuildRangeInfo, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
	}

	//temporary buffers are destroyed automatically
}

void Core::App::createAccelerationStructure(VkAccelerationStructureTypeKHR asType, AccelerationStructure& accelStructure, VkAccelerationStructureGeometryKHR& asGeometry, VkAccelerationStructureBuildRangeInfoKHR& asBuildRangeInfo, VkBuildAccelerationStructureFlagsKHR flags) {
	auto alignUp = [](auto value, size_t alignment) noexcept { return ((value + alignment - 1) & ~(alignment - 1)); };
	
	VkAccelerationStructureBuildGeometryInfoKHR asBuildInfo{
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		.type = asType,
		.flags = flags,
		.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
		.geometryCount = 1,
		.pGeometries = &asGeometry
	};
	
	std::vector<uint32_t> maxPrimCount(1);
	maxPrimCount[0] = asBuildRangeInfo.primitiveCount;

	if(asType == VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR)
		std::cout << "Primitive Count: " << maxPrimCount[0] << std::endl;

	VkAccelerationStructureBuildSizesInfoKHR asBuildSize{ .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
	vkGetAccelerationStructureBuildSizesKHR(device.getDevice(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &asBuildInfo, maxPrimCount.data(), &asBuildSize);

	VkDeviceSize scratchSize = alignUp(asBuildSize.buildScratchSize, device.getAccelProperties()->minAccelerationStructureScratchOffsetAlignment);

	Buffer scratchBuffer{
		device,
		asBuildSize.buildScratchSize,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
		device.getAccelProperties()->minAccelerationStructureScratchOffsetAlignment
	};

	device.createBuffer(asBuildSize.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &accelStructure.buffer, &accelStructure.memory);

	VkAccelerationStructureCreateInfoKHR createInfo{
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
		.buffer = accelStructure.buffer,
		.size = asBuildSize.accelerationStructureSize,
		.type = asType,
	};

	vkCreateAccelerationStructureKHR(device.getDevice(), &createInfo, nullptr, &accelStructure.handle);

	VkCommandBuffer cmd = device.beginSingleTimeCommands();
	asBuildInfo.dstAccelerationStructure = accelStructure.handle;
	asBuildInfo.scratchData = { .deviceAddress = scratchBuffer.getAddress() };

	VkAccelerationStructureBuildRangeInfoKHR* pBuildRangeInfo = &asBuildRangeInfo;
	vkCmdBuildAccelerationStructuresKHR(cmd, 1, &asBuildInfo, &pBuildRangeInfo);

	VkAccelerationStructureDeviceAddressInfoKHR info{ .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
													.accelerationStructure = accelStructure.handle };
	accelStructure.address = vkGetAccelerationStructureDeviceAddressKHR(device.getDevice(), &info);

	device.endSingleTimeCommands(cmd);

	//The scratch buffer will be deleted automatically
}

void Core::App::createUniformBuffers() {
	for (uint32_t i = 0; i < uniformBuffers.size(); i++) {
		uniformBuffers[i] = std::make_unique<Buffer>(
			device,
			sizeof(Uniform),
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
		);

		uniformBuffers[i]->map();
	}
}

//-------------------- INPUT SHADER --------------------
void Core::App::readShader(std::string file, VkShaderModule* module) {
	std::vector<char> code = readShaderFile(file);

	createShaderModule(code, module);
}

std::vector<char> Core::App::readShaderFile(std::string& path) {
	std::ifstream file(path, std::ios::ate | std::ios::binary);

	if (!file.is_open()) throw std::runtime_error("failed to open: " + path);

	size_t size = static_cast<size_t>(file.tellg());
	std::vector<char> buf(size);
	file.seekg(0);
	file.read(buf.data(), size);
	file.close();

	return buf;
}

void Core::App::createShaderModule(const std::vector<char>& code, VkShaderModule* module) {
	VkShaderModuleCreateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	info.codeSize = code.size();
	info.pCode = reinterpret_cast<const uint32_t*>(code.data());

	VK_CHECK_RESULT(vkCreateShaderModule(device.getDevice(), &info, nullptr, module), "failed to create Shader module");
}

void Core::App::createCommandBuffers() {
	commandBuffers.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);

	VkCommandBufferAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandPool = device.getCommandPool();
	allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

	VK_CHECK_RESULT(vkAllocateCommandBuffers(device.getDevice(), &allocInfo, commandBuffers.data()), "failed to allocate command buffers");
}

void Core::App::recreateSwapChain() {
	auto extent = window.getExtent();

	while (extent.width == 0 || extent.height == 0) {
		extent = window.getExtent();
		glfwWaitEvents();
	}

	vkDeviceWaitIdle(device.getDevice());

	if (swapChain == nullptr)
		swapChain = std::make_unique<SwapChain>(device, extent);
	else {
		std::shared_ptr<SwapChain> oldSwapChain = std::move(swapChain);
		swapChain = std::make_unique<SwapChain>(device, extent, oldSwapChain);

		if (!oldSwapChain->compareSwapFormats(*swapChain.get())) throw std::runtime_error("Swap Chain image format has changed!");
	}
}

void Core::App::recreateStorageImage() {
	destroyStorageImage();
	createStorageImage();
	createRayTracingDescriptorSets();


}

// -------------------- RENDER FUNCTIONS --------------------

VkCommandBuffer Core::App::beginFrame() {
	assert(!frameStarted && "Can't call beginFrame while already in progress!");

	auto result = swapChain->acquireNextImage(&currentImageIndex);
	if (result == VK_ERROR_OUT_OF_DATE_KHR) {
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

void Core::App::endFrame() {
	assert(frameStarted && "Can't call endFrame while frame is not in progress");

	//end command buffer
	auto commandBuffer = commandBuffers[currentFrameIndex];
	VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer), "failed to record buffer!");

	//submit command buffers
	auto result = swapChain->submitCommandBuffers(&commandBuffer, &currentImageIndex);
	//check results of the rendering and recreate the swap chain if the window has changed it's size
	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || window.wasWindowResized()) {
		window.resetWindowResizeFlag();
		discardImage = true;
		recreateSwapChain();
		recreateStorageImage();
	} else VK_CHECK_RESULT(result, "failed to present swap chain image");

	//reset frameStarted and updated frame index
	frameStarted = false;
	currentFrameIndex = (currentFrameIndex + 1) % SwapChain::MAX_FRAMES_IN_FLIGHT;
}

//Unused in the ray tracing rendering pipeline
void Core::App::beginRenderPass(VkCommandBuffer buffer) {
	assert(frameStarted && "Can't call beginRenderPass while frame is not in progress");
	assert(buffer == commandBuffers[currentFrameIndex] && "Can't begin render pass on command buffer from a different frame");

	VkRenderPassBeginInfo renderPassInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
	renderPassInfo.renderPass = swapChain->getRenderPass();
	renderPassInfo.framebuffer = swapChain->getFrameBuffer(currentImageIndex);

	renderPassInfo.renderArea = {
		.offset = {0, 0},
		.extent = swapChain->getSwapChainExtent()
	};

	std::array<VkClearValue, 2> clearValues{};
	clearValues[0].color = { 0.1f, 0.1f, 0.1f, 1.0f };
	clearValues[1].depthStencil = { 1.0f, 0 };
	renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
	renderPassInfo.pClearValues = clearValues.data();

	vkCmdBeginRenderPass(buffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

	VkViewport viewport{
		.x = 0.0f,
		.y = 0.0f,
		.width = static_cast<float>(swapChain->getSwapChainExtent().width),
		.height = static_cast<float>(swapChain->getSwapChainExtent().height),
		.minDepth = 0.0f,
		.maxDepth = 1.0f
	};

	VkRect2D scissor{ {0, 0}, swapChain->getSwapChainExtent() };
	vkCmdSetViewport(buffer, 0, 1, &viewport);
	vkCmdSetScissor(buffer, 0, 1, &scissor);
}

//unused in the ray tracing pipeline
void Core::App::endRenderPass(VkCommandBuffer buffer) {
	assert(frameStarted && "Can't call beginRenderPass while frame is not in progress");
	assert(buffer == commandBuffers[currentFrameIndex] && "Can't begin render pass on command buffer from a different frame");

	vkCmdEndRenderPass(buffer);
}

void Core::App::copyImageToSwapChain(VkCommandBuffer buffer, VkImage swapChainImage, VkImage storageImage, VkExtent2D size) {
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

	if(!discardImage)
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

void Core::App::rayTraceScene() {
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
		info.projInverse =  glm::inverse(glm::transpose(camera.getProjection())); //projection matrix is already inversed

		uniformBuffers[currentFrameIndex]->writeToBuffer(&info);
		uniformBuffers[currentFrameIndex]->flush();

		//Call the ray tracing command
		VkExtent2D size = swapChain->getSwapChainExtent();
		vkCmdTraceRaysKHR(buffer, &raygenRegion, &missRegion, &hitRegion, &callableRegion, size.width, size.height, 1);

		//The image will later be denoised by DLSS Ray Reconstruction and then upscaled by DLSS Super Resolution

		//copy rendered image to swap chain for presentation
		copyImageToSwapChain(buffer, swapChain->getImage(currentImageIndex), storageImage.image, size);

		endFrame();
	}

	discardImage = false;
}

// -------------------- DESTRUCTION FUNCTIONS --------------------

void Core::App::destroyStorageImage() {
	vkDestroyImageView(device.getDevice(), storageImage.imageView, nullptr);
	vkDestroyImage(device.getDevice(), storageImage.image, nullptr);
	vkFreeMemory(device.getDevice(), storageImage.imageMemory, nullptr);
}

void Core::App::destroyAccelerationStructures() {
	
	vkDestroyAccelerationStructureKHR(device.getDevice(), tlasAccel.handle, nullptr);
	vkDestroyBuffer(device.getDevice(), tlasAccel.buffer, nullptr);
	vkFreeMemory(device.getDevice(), tlasAccel.memory, nullptr);

	for (uint32_t i = 0; i < blasAccel.size(); i++) {
		vkDestroyAccelerationStructureKHR(device.getDevice(), blasAccel[i].handle, nullptr);
		vkDestroyBuffer(device.getDevice(), blasAccel[i].buffer, nullptr);
		vkFreeMemory(device.getDevice(), blasAccel[i].memory, nullptr);
	}
}

void Core::App::freeCommandBuffers() {
	vkFreeCommandBuffers(device.getDevice(), device.getCommandPool(), SwapChain::MAX_FRAMES_IN_FLIGHT, commandBuffers.data());
}


void Core::App::loadModel(std::string path) {
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;

	tinyobj::attrib_t attributes;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string err;

	if (!tinyobj::LoadObj(&attributes, &shapes, &materials, &err, path.c_str()))
		throw std::runtime_error(err);

	std::unordered_map<Vertex, uint32_t> uniqueVertices{};
	for (const auto& shape : shapes) {
		for (const auto& index : shape.mesh.indices) {
			Vertex vertex{};
			if (index.vertex_index >= 0) {
				vertex = {
					attributes.vertices[3 * index.vertex_index + 0],
					attributes.vertices[3 * index.vertex_index + 1],
					attributes.vertices[3 * index.vertex_index + 2],
				};
			}

			if (uniqueVertices.count(vertex) == 0) {
				uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
				vertices.push_back(vertex);
			}

			indices.push_back(uniqueVertices[vertex]);
		}
	}

	meshes.push_back(Mesh{ device, vertices, indices });
}

// -------------------- TEST FUNCTIONS --------------------
void Core::App::generateMesh() {
	std::vector<Core::Vertex> vertices = {
		Vertex{-1.f, 1.f, 1.f},
		Vertex{1.f, 1.f, 1.f},
		Vertex{1.f, -1.f, 1.f},
		Vertex{-1.f, -1.f, 1.f}
	};

	std::vector<uint32_t> indices = {
		0, 1, 2,
		0, 2, 3
	};

	meshes.push_back(Mesh(device, vertices, indices));
}

Core::Mesh::Mesh(Device& device, std::vector<Vertex> vertices, std::vector<uint32_t> indices) 
	: vertices(vertices),
	  indices(indices)
{
	{
		uint32_t bufferSize = sizeof(vertices[0]) * vertices.size();
		Buffer stagingBuffer{
			device, bufferSize,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
		};

		stagingBuffer.map();
		stagingBuffer.writeToBuffer((void*)vertices.data());

		vertexBuffer = std::make_unique<Buffer>(
			device,
			bufferSize,
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
		);

		device.copyBuffer(stagingBuffer.getBuffer(), vertexBuffer->getBuffer(), bufferSize);
	}

	{
		uint32_t bufferSize = sizeof(indices[0]) * indices.size();
		Buffer stagingBuffer{
			device, bufferSize,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
		};

		stagingBuffer.map();
		stagingBuffer.writeToBuffer((void*)indices.data());

		indexBuffer = std::make_unique<Buffer>(
			device,
			sizeof(uint32_t) * indices.size(),
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
		);

		device.copyBuffer(stagingBuffer.getBuffer(), indexBuffer->getBuffer(), bufferSize);
	}
}