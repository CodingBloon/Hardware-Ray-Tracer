#pragma once

#include "vulkan/vulkan.h"
#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>

namespace RayTracing {
	class MeshInstance {
	public:
		MeshInstance(uint32_t meshId, 
			uint32_t materialId, 
			glm::vec3 position = glm::vec3(), 
			glm::vec3 rotation = glm::vec3()) 
			: meshId(meshId), 
			materialId(materialId), 
			position(position), 
			rotation(rotation) {
			calculateTransformation();
		}
		~MeshInstance() {}

		void setPosition(glm::vec3 position) { this->position = position; calculateTransformation(); }
		void setRotation(glm::vec3 rotation) { this->rotation = rotation;  calculateTransformation(); }
		void setMeshId(uint32_t meshId) { this->meshId = meshId; }
		void setMaterialId(uint32_t materialId) { this->materialId = materialId; }

		inline glm::vec3 getPosition() { return position; }
		inline glm::vec3 getRotation() { return rotation; }
		inline VkTransformMatrixKHR getTransformation() { return transform; }
		inline uint32_t getMeshId() { return meshId; }
		inline uint32_t getMaterialId() { return materialId; }
	private:
		void calculateTransformation() {

			transform = {
			1.0f, 0.0f, 0.0f, position.x,
			0.0f, 1.0f, 0.0f, position.y,
			0.0f, 0.0f, 1.0f, position.z };
		}
	private:
		uint32_t meshId;
		uint32_t materialId;
		glm::vec3 position;
		glm::vec3 rotation;
		VkTransformMatrixKHR transform;
	};
}