#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <iostream>

namespace Core {
	class Window {

	public:
		Window(int width, int height, std::string title, bool useFullscreen);
		~Window();

		bool shouldClose() { return glfwWindowShouldClose(window); }
		void createWindowSurface(VkInstance instance, VkSurfaceKHR* surface);
		VkExtent2D getExtent() { return { static_cast<uint32_t>(width), static_cast<uint32_t>(height) }; }
		bool wasWindowResized() { return frameBufferResized; }
		void resetWindowResizeFlag() { frameBufferResized = false; }
		GLFWwindow* getGLFWWindow() { return window; }
		void setWindowTitle(std::string title) { this->title = title; glfwSetWindowTitle(window, title.c_str()); }
	private:
		static void frameBufferResizeCallback(GLFWwindow* window, int width, int heigth);
		void initWindow();

	private:
		int height, width;
		std::string title;
		bool useFullscreen;

		GLFWwindow* window;
		bool frameBufferResized = false;
		float aspectRatio = 1.0f;
	};
}