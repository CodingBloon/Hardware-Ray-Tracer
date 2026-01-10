#pragma once

#include "vulkan/vulkan.h"
#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <math.h>

namespace RayTracing {
	class MeshInstance {
	public:
		MeshInstance(uint32_t meshId, 
			uint32_t materialId, 
			glm::vec3 position = glm::vec3(), 
			glm::vec3 rotation = glm::vec3(),
			glm::vec3 scale = glm::vec3(1, 1, 1)) 
			: meshId(meshId), 
			materialId(materialId), 
			position(position), 
			rotation(rotation),
			scale(scale) {
			calculateTransformation();
		}
		~MeshInstance() {}

		void setPosition(glm::vec3 position) { this->position = position; calculateTransformation(); }
		void setRotation(glm::vec3 rotation) { this->rotation = rotation;  calculateTransformation(); }
		void setScale(glm::vec3 scale) { this->scale = scale; calculateTransformation(); }
		void setMeshId(uint32_t meshId) { this->meshId = meshId; }
		void setMaterialId(uint32_t materialId) { this->materialId = materialId; }

		inline glm::vec3 getPosition() { return position; }
		inline glm::vec3 getRotation() { return rotation; }
		inline VkTransformMatrixKHR getTransformation() { return transform; }
		inline uint32_t getMeshId() { return meshId; }
		inline uint32_t getMaterialId() { return materialId; }
	private:
		void calculateTransformation() {
			/*const float c3 = glm::cos(rotation.z);
			const float s3 = glm::sin(rotation.z);
			const float c2 = glm::cos(rotation.x);
			const float s2 = glm::sin(rotation.x);
			const float c1 = glm::cos(rotation.y);
			const float s1 = glm::sin(rotation.y);

			glm::mat4 mat = {
				{
					scale.x * (c1 * c3 + s1 * s2 * s3),
					scale.x * (c2 * s3),
					scale.x * (c1 * s2 * s3 - c3 * s1),
					0.0f
				},
				{
					scale.y * (c3 * s1 * s2 - c1 * s3),
					scale.y * (c2 * c3),
					scale.y * (c1 * c3 * s2 + s1 * s3),
					0.0f
				},
				{
					scale.z * (c2 * s1),
					scale.z * (-s2),
					scale.z * (c1 * c2),
					0.0f
				},
				{
					position.x,
					position.y,
					position.z,
					0.0f
				}
			};

			memcpy(&transform, glm::value_ptr(glm::transpose(mat)), sizeof(transform));

			/*glm::mat4 temp = {
				scale.x, 0.0f, 0.0f, position.x,
				0.0f, scale.y, 0.0f, position.y,
				0.0f, 0.0f, scale.z, position.z,
				0.0f, 0.0f, 0.0f, 1.0f
			};*/

			transform = {
			scale.x,	0.0f,		0.0f, position.x,
			0.0f,		scale.y,	0.0f, position.y,
			0.0f,		0.0f,		scale.z, position.z };
		}
	private:
		uint32_t meshId;
		uint32_t materialId;
		glm::vec3 position;
		glm::vec3 rotation;
		glm::vec3 scale;
		VkTransformMatrixKHR transform;
	};
}