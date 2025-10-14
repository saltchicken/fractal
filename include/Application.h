#pragma once
#include "Config.h"
#include <string>
#include <memory>
#include <filesystem>

// Forward declarations of main components
class Window;
class Renderer;
class Animator;

class Application {
public:
    Application(int argc, char* argv[]);
    ~Application();
    void run();

private:
    void loadConfig();
    void check_for_config_updates();
    void handleWindowResize(int width, int height);

    std::unique_ptr<Window> m_window;
    std::unique_ptr<Renderer> m_renderer;
    std::unique_ptr<Animator> m_animator;

    Config m_config;
    std::string m_config_path;

    // State for hot-reloading
    static constexpr float HOT_RELOAD_INTERVAL = 1.0f;
    float m_hot_reload_check_timer = 0.0f;
    std::filesystem::file_time_type m_last_config_write_time;

    double m_last_frame_time = 0.0;

    unsigned int m_width = 0;
    unsigned int m_height = 0;
};
