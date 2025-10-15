#pragma once
#include <functional>
#include <string>
// Forward declare GLFWwindow to avoid including glfw3.h in the header
struct GLFWwindow;
class Window {
public:
    using ResizeCallback = std::function<void(int, int)>;
    using KeyCallback = std::function<void(int, int, int, int)>;
    Window();
    ~Window();
    bool init(unsigned int width, unsigned int height, const std::string& title, bool transparent, bool visible);
    bool shouldClose() const;
    void swapBuffers();
    void pollEvents();
    void setResizeCallback(const ResizeCallback& callback);
    void setKeyCallback(const KeyCallback& callback);
    GLFWwindow* getNativeWindow() const { return m_window; }
private:
    // Static callbacks passed to GLFW, which then call our member functions.
    static void framebuffer_size_callback(GLFWwindow* window, int width, int height);
    static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
    // The actual member function that handles the resize logic.
    void onWindowResize(int width, int height);
    GLFWwindow* m_window = nullptr;
    ResizeCallback m_resize_callback;
    KeyCallback m_key_callback;
};
