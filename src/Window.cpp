#include "Window.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
void Window::framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self) {
        self->onWindowResize(width, height);
    }
}
void Window::key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self && self->m_key_callback) {
        self->m_key_callback(key, scancode, action, mods);
    }
}
void Window::onWindowResize(int width, int height) {
    if (width > 0 && height > 0 && m_resize_callback) {
        glViewport(0, 0, width, height);
        m_resize_callback(width, height);
    }
}
Window::Window() = default;
Window::~Window() {
    if (m_window) {
        glfwDestroyWindow(m_window);
    }
    glfwTerminate();
}
bool Window::init(unsigned int width, unsigned int height, const std::string& title, bool transparent, bool visible) {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return false;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, transparent ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, visible ? GLFW_TRUE : GLFW_FALSE);
    std::string title_str;
    if (transparent) {
        title_str = title + " - Transparent";
    } else {
        title_str = title;
    }
    m_window = glfwCreateWindow(width, height, title_str.c_str(), NULL, NULL);
    if (!m_window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return false;
    }
    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, framebuffer_size_callback);
    glfwSetKeyCallback(m_window, key_callback);
    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(0); // Disable vsync blocking
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        return false;
    }
    glViewport(0, 0, width, height);
    glEnable(GL_PROGRAM_POINT_SIZE);
    return true;
}
bool Window::shouldClose() const {
    return glfwWindowShouldClose(m_window);
}
void Window::swapBuffers() {
    glfwSwapBuffers(m_window);
}
void Window::pollEvents() {
    glfwPollEvents();
}
void Window::setResizeCallback(const ResizeCallback& callback) {
    m_resize_callback = callback;
}
void Window::setKeyCallback(const KeyCallback& callback) {
    m_key_callback = callback;
}
