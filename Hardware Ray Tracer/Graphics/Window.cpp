#include "Window.h"

Core::Window::Window(int width, int height, std::string title) : width(width), height(height), title(title) {
	initWindow();
}

Core::Window::~Window() {
	glfwDestroyWindow(window);
	glfwTerminate();
}

void Core::Window::createWindowSurface(VkInstance instance, VkSurfaceKHR* surface) {
	if (glfwCreateWindowSurface(instance, window, nullptr, surface) != VK_SUCCESS)
		throw std::runtime_error("Failed to create window surface!");
}

void Core::Window::initWindow() {
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GL_TRUE);

	window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
	glfwSetWindowUserPointer(window, this);
}
