#include "Scene.h"

#include <span>

namespace std {
	template<>
	struct hash<RayTracing::Vertex> {
		size_t operator()(RayTracing::Vertex const& vert) const {
			size_t seed = 0;
			hashCombine(seed, vert.pos[0], vert.pos[1], vert.pos[2], 0);
			return seed;
		}
	};
}

RayTracing::Scene::Scene(Core::Device& device) : device(device) {}
RayTracing::Scene::~Scene() {
	vkDestroyAccelerationStructureKHR(device.getDevice(), tlasAccel.handle, nullptr);
	vkDestroyBuffer(device.getDevice(), tlasAccel.buffer, nullptr);
	vkFreeMemory(device.getDevice(), tlasAccel.memory, nullptr);

	for (uint32_t i = 0; i < blasAccel.size(); i++) {
		vkDestroyAccelerationStructureKHR(device.getDevice(), blasAccel[i].handle, nullptr);
		vkDestroyBuffer(device.getDevice(), blasAccel[i].buffer, nullptr);
		vkFreeMemory(device.getDevice(), blasAccel[i].memory, nullptr);
	}
}

void RayTracing::Scene::loadModel(std::string path) {
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	
	tinyobj::attrib_t attributes;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string err;

	if (!tinyobj::LoadObj(&attributes, &shapes, &materials, &err, path.c_str())) {
		std::cout << "[ERROR] Scene: " << err << std::endl;
		throw std::runtime_error(err);
	}

	std::unordered_map<Vertex, uint32_t> uniqueVertices{};
	for (const auto& shape : shapes) {
		for (const auto& index : shape.mesh.indices) {
			Vertex vertex{};
			if (index.vertex_index >= 0) {
				vertex.pos[0] = attributes.vertices[3 * index.vertex_index + 0];
				vertex.pos[1] = -attributes.vertices[3 * index.vertex_index + 1];
				vertex.pos[2] = attributes.vertices[3 * index.vertex_index + 2];
			}

			if (index.normal_index >= 0) {
				vertex.normal[0] = attributes.normals[3 * index.normal_index + 0];
				vertex.normal[1] = -attributes.normals[3 * index.normal_index + 1];
				vertex.normal[2] = attributes.normals[3 * index.normal_index + 2];
			}

			if (index.texcoord_index >= 0) {
				vertex.uv[0] = attributes.texcoords[2 * index.texcoord_index + 0];
				vertex.uv[1] = attributes.texcoords[2 * index.texcoord_index + 1];
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

void RayTracing::Scene::createInstance(uint32_t meshId, uint32_t materialId, glm::vec3 position, glm::vec3 rotation, glm::vec3 scale) {
	instances.push_back(MeshInstance(meshId, materialId, position, rotation, scale));
}

void RayTracing::Scene::createMaterial(glm::vec3 color, float metallic, float roughness, glm::vec3 emissiveColor, float emissionStrength) {
	materials.push_back(Material{
		.color = {color.x, color.y, color.z},
		.metallic = metallic,
		.roughness = roughness
	});
}

void RayTracing::Scene::createLight(glm::vec3 position, glm::vec3 color, float intensity) {
	lights.push_back(
		Light{
			{position.x, position.y, position.z},
			{color.x, color.y, color.z},
			intensity,
			LightType::POINT
		}
	);
}


void RayTracing::Scene::build() {
	BUILD("SCENE", 0, 7, "Creating Bottom Level Acceleration Structure...");
	createBottomAS();
	BUILD("SCENE", 1, 7, "Creating TOP Level Acceleration Structure...");
	createTopAS();

	BUILD("SCENE", 2, 7, "Creating materials...");
	createMaterials();
	BUILD("SCENE", 3, 7, "Creating lights...");
	createLights();

	BUILD("SCENE", 4, 7, "Creating Sky...");
	createSky();

	BUILD("SCENE", 5, 7, "Creating scene information...");
	createSceneInformation();
	BUILD("SCENE", 6, 7, "Creating scene information buffer...");
	createSceneInfoBuffer();

	BUILD("SCENE", 7, 7, "Scene created!");
}

void RayTracing::Scene::destroyInstance(uint32_t instanceID) {
	instances[instanceID] = instances[instances.size() - 1];
	instances.pop_back();
}

void RayTracing::Scene::unloadModel(uint32_t meshId) {}

void RayTracing::Scene::destroyLight(uint32_t lightId) {
}

void RayTracing::Scene::destroyMaterial(uint32_t materialId) {
}

void RayTracing::Scene::prepareRendering() {
	std::cout << "Not implemented!" << std::endl;
	throw std::runtime_error("LBVH not implemented!");
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
	};

	geometry = VkAccelerationStructureGeometryKHR{
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
		.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
		.geometry = {.triangles = triangles },
		.flags = VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR | VK_GEOMETRY_OPAQUE_BIT_KHR,
	};

	rangeInfo = VkAccelerationStructureBuildRangeInfoKHR{ .primitiveCount = triangeCount };
}

void RayTracing::Scene::createBottomAS() {
	blasAccel.resize(meshes.size());

	for (uint32_t i = 0; i < meshes.size(); i++) {
		VkAccelerationStructureGeometryKHR asGeometry{};
		VkAccelerationStructureBuildRangeInfoKHR asBuildRangeInfo{};

		primitiveToGeometry(meshes[i], asGeometry, asBuildRangeInfo);

		createAccelerationStructure(VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, blasAccel[i], asGeometry, asBuildRangeInfo, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
	}
}

void RayTracing::Scene::createTopAS() {
	std::vector<VkAccelerationStructureInstanceKHR> tlasInstances;
	tlasInstances.reserve(instances.size());

	for (uint32_t i = 0; i < instances.size(); i++) {
		uint32_t meshId = instances[i].getMeshId();

		VkAccelerationStructureInstanceKHR asInstance{
			.transform = instances[i].getTransformation(),
			.instanceCustomIndex = meshId,
			.mask = 0xFF,
			.instanceShaderBindingTableRecordOffset = 0,
			.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_CULL_DISABLE_BIT_NV,
			.accelerationStructureReference = blasAccel[meshId].address,
		};
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

		asBuildRangeInfo = { .primitiveCount = static_cast<uint32_t>(instances.size()) };

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

	VkDeviceSize scratchSize = alignUp(asBuildSize.buildScratchSize, device.getAccelProperties()->minAccelerationStructureScratchOffsetAlignment);

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

void RayTracing::Scene::createMaterials() {
	uint64_t size = materials.size() * sizeof(Material);

	materialBuffer = std::make_unique<Core::Buffer>(
		device, size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
	);

	stageInformation(materials.data(), size, materialBuffer->getBuffer());
}

void RayTracing::Scene::createLights() {
	uint64_t size = lights.size() * sizeof(Light);

	lightBuffer = std::make_unique<Core::Buffer>(
		device, size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
	);

	stageInformation(lights.data(), size, lightBuffer->getBuffer());
}

void RayTracing::Scene::createSky() {
	SkyInfo info{
		.skyColor = {0.17f, 0.24f, 0.31f},
		.horizonColor = {1.f, 0.5f, 0.31f},
		.groundColor = {0.1f, 0.06f, 0.04f},
		.sunDirection = {0.9f, -0.1f, 0.0f},
		.upDirection = {0.f, -1.f, 0.f},

		.brightness = 0.8f,
		.horizonSize = 0.5f,
		.angularSize = 0.08f,
		.glowIntensity = 2.5f,
		.glowSharpness = 0.2f,
		.glowSize = 0.2f,
		.lightRadiance = 0.7f
	};

	skyBuffer = std::make_unique<Core::Buffer>(
		device, sizeof(SkyInfo), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
	);

	stageInformation(&info, sizeof(SkyInfo), skyBuffer->getBuffer());
}

void RayTracing::Scene::createSceneInformation() {
	std::vector<InstanceInfo> instanceInfo;
	instanceInfo.resize(instances.size());

	for (uint32_t i = 0; i < instances.size(); i++) {
		uint32_t meshId = instances[i].getMeshId();
		
		instanceInfo[i] = {
			.vertexAddress = meshes[meshId].vertexBuffer->getAddress(),
			.indexAddress = meshes[meshId].indexBuffer->getAddress(),
			.materialId = instances[i].getMaterialId()
		};
	}

	instanceBuffer = std::make_unique<Core::Buffer>(
		device, sizeof(InstanceInfo) * instanceInfo.size(), VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
	);

	stageInformation(instanceInfo.data(), sizeof(InstanceInfo) * instanceInfo.size(), instanceBuffer->getBuffer());
}

void RayTracing::Scene::createSceneInfoBuffer() {
	std::cout << "Lights: " << lights.size() << std::endl;

	SceneBufferInfo info{
		.mBuf = materialBuffer->getAddress(),
		.mStride = sizeof(Material),
		
		.lBuf = lightBuffer->getAddress(),
		.lStride = sizeof(Light),
		.lCount = lights.size(),

		.vStride = sizeof(Vertex),

		.sBuf = instanceBuffer->getAddress(),
		.sStride = sizeof(InstanceInfo),

		.skyBuf = skyBuffer->getAddress(),
		.skyStride = sizeof(SkyInfo)
	};

	sceneInfoBuffer = std::make_unique<Core::Buffer>(
		device, sizeof(SceneBufferInfo), VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
	);

	stageInformation(&info, sizeof(SceneBufferInfo), sceneInfoBuffer->getBuffer());
}

void RayTracing::Scene::stageInformation(void* data, uint64_t size, VkBuffer dstBuffer) {
	Core::Buffer stagingBuffer{
		device,
		size,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
	};

	stagingBuffer.map();
	stagingBuffer.writeToBuffer(data);

	device.copyBuffer(stagingBuffer.getBuffer(), dstBuffer, size);
}

RayTracing::Mesh::Mesh(Core::Device& device, std::vector<Vertex> vertices, std::vector<uint32_t> indices) : vertices(vertices), indices(indices) {
	
	{
		vertexBuffer = std::make_unique<Core::Buffer>(
			device, 
			sizeof(Vertex) * vertices.size(), 
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_DST_BIT, 
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
		);
		Core::Buffer stagingBuffer{ device, 
			sizeof(Vertex) * vertices.size(), 
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT };

		stagingBuffer.map();
		stagingBuffer.writeToBuffer((void*)vertices.data());

		device.copyBuffer(stagingBuffer.getBuffer(), vertexBuffer->getBuffer(), sizeof(Vertex) * vertices.size());
	}
	{
		indexBuffer = std::make_unique<Core::Buffer>(
			device,
			sizeof(uint32_t) * indices.size(),
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
		);
		Core::Buffer stagingBuffer{
			device,
			sizeof(uint32_t) * indices.size(),
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
		};

		stagingBuffer.map();
		stagingBuffer.writeToBuffer((void*)indices.data());

		device.copyBuffer(stagingBuffer.getBuffer(), indexBuffer->getBuffer(), sizeof(uint32_t) * indices.size());
	}

}
