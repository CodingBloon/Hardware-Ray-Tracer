#include "Scene.h"

RayTracing::Scene::Scene(Core::Device& device) : device(device) {
	
}

void RayTracing::Scene::loadAndAppendModel(std::string path) {

}

void RayTracing::Scene::buildAccelerationStructures() {
	createBottomLevelAS();
}

void RayTracing::Scene::primitiveToGeometry(const Mesh& mesh, VkAccelerationStructureGeometryKHR& geometry, VkAccelerationStructureBuildRangeInfoKHR& rangeInfo) {
	const auto triangeCount = static_cast<uint32_t>(mesh.indices.size() / 3U);

	VkAccelerationStructureGeometryTrianglesDataKHR triangles{
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
		.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
		.vertexData = {.deviceAddress = mesh.vertexBuffer->getAddress()},
		.vertexStride = sizeof(Vertex),
		.maxVertex = static_cast<uint32_t>(mesh.vertices.size()) - 1,
		.indexType = VK_INDEX_TYPE_UINT32,
		.indexData = {.deviceAddress = mesh.indexBuffer->getAddress() },
		.transformData = {.deviceAddress = 0 }
	};

	geometry = VkAccelerationStructureGeometryKHR{
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
		.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
		.geometry = {.triangles = triangles },
		.flags = VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR | VK_GEOMETRY_OPAQUE_BIT_KHR,
	};

	rangeInfo = VkAccelerationStructureBuildRangeInfoKHR{ .primitiveCount = triangeCount };
}

void RayTracing::Scene::createBottomLevelAS() {
	blasAccel.resize(meshes.size());
	
	for (uint32_t i = 0; i < meshes.size(); i++) {
		VkAccelerationStructureGeometryKHR asGeometry{};
		VkAccelerationStructureBuildRangeInfoKHR asBuildRangeInfo{};

		primitiveToGeometry(meshes[i], asGeometry, asBuildRangeInfo);
		createAccelerationStructure(VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, blasAccel[i], asGeometry, asBuildRangeInfo, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
	}
}

void RayTracing::Scene::createTopLevelAS() {
	std::vector<VkAccelerationStructureInstanceKHR> tlasInstances;
	tlasInstances.reserve(meshes.size());

	for (uint32_t i = 0; i < blasAccel.size(); i++) {
		VkAccelerationStructureInstanceKHR asInstance{};
		asInstance.transform = transformMatrix;
		asInstance.instanceCustomIndex = i;
		asInstance.accelerationStructureReference = blasAccel[i].address;
		asInstance.instanceShaderBindingTableRecordOffset = 0;
		asInstance.mask = 0xFF;
		//asInstance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_CULL_DISABLE_BIT_NV,
		tlasInstances.emplace_back(asInstance);
	}

	VkCommandBuffer cmd = device.beginSingleTimeCommands();

	constexpr size_t instanceAlignment = 16;

	Core::Buffer stagingBuffer{
		device,
		std::span<VkAccelerationStructureInstanceKHR const>(tlasInstances).size_bytes(),
		VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		instanceAlignment
	};

	Core::Buffer tlasInstanceBuffer{
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
			.geometry = {.instances = geometryInstances }
		};

		asBuildRangeInfo = { .primitiveCount = static_cast<uint32_t>(meshes.size()) };

		createAccelerationStructure(VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, tlasAccel, asGeometry, asBuildRangeInfo, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
	}
}

void RayTracing::Scene::createAccelerationStructure(VkAccelerationStructureTypeKHR asType,
	AccelerationStructure& accelStructure,
	VkAccelerationStructureGeometryKHR& asGeometry,
	VkAccelerationStructureBuildRangeInfoKHR& asBuildRangeInfo,
	VkBuildAccelerationStructureFlagsKHR flags) {
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

	VkAccelerationStructureBuildSizesInfoKHR asBuildSize{ .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
	vkGetAccelerationStructureBuildSizesKHR(device.getDevice(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &asBuildInfo, maxPrimCount.data(), &asBuildSize);

	Core::Buffer scratchBuffer{
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
}
