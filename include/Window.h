#pragma once
#include <functional>
#include <string>

// Forward declare GLFWwindow to avoid including glfw3.h in the header
struct GLFWwindow;

class Window {
public:
    using ResizeCallback = std::function<void(int, int)>;

    Window();
    ~Window();

    bool init(unsigned int width, unsigned int height, const std::string& title, bool transparent);

    bool shouldClose() const;
    void processInput();
    void swapBuffers();
    void pollEvents();

    void setResizeCallback(const ResizeCallback& callback);

    GLFWwindow* getNativeWindow() const { return m_window; }

private:
    // Static callback passed to GLFW, which then calls our member function.
    static void framebuffer_size_callback(GLFWwindow* window, int width, int height);
    // The actual member function that handles the resize logic.
    void onWindowResize(int width, int height);

    GLFWwindow* m_window = nullptr;
    ResizeCallback m_resize_callback;
    bool m_keys[1024]; // Basic input state
};
