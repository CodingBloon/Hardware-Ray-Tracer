#include "Window.h"

Core::Window::Window(int width, int height, std::string title, bool useFullscreen) : width(width), height(height), title(title), useFullscreen(useFullscreen) {
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

void Core::Window::frameBufferResizeCallback(GLFWwindow* window, int width, int heigth) {
	auto w = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
	w->frameBufferResized = true;
	w->width = width;
	w->height = heigth;
}

void Core::Window::initWindow() {
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GL_TRUE);

	if (useFullscreen) {
		GLFWmonitor* monitor = glfwGetPrimaryMonitor();
		int m_width = 0, m_height = 0;
		const GLFWvidmode* mode = glfwGetVideoMode(monitor);
		window = glfwCreateWindow(mode->width, mode->height, title.c_str(), monitor, nullptr);
		glfwSetWindowUserPointer(window, this);
		glfwSetFramebufferSizeCallback(window, frameBufferResizeCallback);
		return;
	}

	window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
	glfwSetWindowUserPointer(window, this);
	glfwSetFramebufferSizeCallback(window, frameBufferResizeCallback);
}
