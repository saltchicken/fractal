#pragma once
#include "Config.h"
#include <string>
#include <memory>
#include <filesystem>
#include <vector>
#include <cstdio> // For FILE*
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
    void onKey(int key, int scancode, int action, int mods);
    void startVideoRender();
    void writeVideoFrame();
    void endVideoRender();
    std::unique_ptr<Window> m_window;
    std::unique_ptr<Renderer> m_renderer;
    std::unique_ptr<Animator> m_animator;
    
    // State management for interpolation
    Config m_config;      // The current, interpolated config sent to the renderer
    Config m_source_config; // The config state when an interpolation begins
    Config m_target_config; // The target state loaded from the config file
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
    double m_target_frame_time = 0.0;
    unsigned int m_width = 0;
    unsigned int m_height = 0;
    bool m_is_transparent = false;
    // New state
    bool m_is_paused = false;
    // Video rendering state
    bool m_is_video_render_mode = false;
    std::string m_video_output_path;
    float m_video_duration = 0.0f;
    unsigned int m_video_fps = 60;
    float m_video_time_elapsed = 0.0f;
    FILE* m_ffmpeg_pipe = nullptr;
    std::vector<unsigned char> m_video_frame_buffer;
};
