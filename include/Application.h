#pragma once
#include "Config.h"
#include <string>
#include <memory>
#include <filesystem>

// --- ADDED BACK: Forward declarations of main components ---
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
    
    // State management for interpolation
    Config m_config;          // The current, interpolated config sent to the renderer
    Config m_source_config;   // The config state when an interpolation begins
    Config m_target_config;   // The target state loaded from the config file
    float m_param_interpolation_alpha = 1.0f;
    static constexpr float PARAM_INTERPOLATION_DURATION = 0.5f; // Duration in seconds

    std::string m_config_path;

    // State for hot-reloading
    static constexpr float HOT_RELOAD_INTERVAL = 1.0f;
    float m_hot_reload_check_timer = 0.0f;
    std::filesystem::file_time_type m_last_config_write_time;

    unsigned int m_frame_counter = 0;
    double m_fps_timer = 0.0;
    
    double m_last_frame_time = 0.0;
    unsigned int m_width = 0;
    unsigned int m_height = 0;

    bool m_is_transparent = false;
};
