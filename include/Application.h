#pragma once
#include "Shader.h"
#include "Config.h"
#include <string>
#include <vector>
#include <random>
#include <filesystem> 
#include <glad/glad.h> // Include glad to get GLint type

// Using forward declaration for GLFWwindow
struct GLFWwindow;

struct Point {
    glm::vec4 position;
    glm::vec4 color;
};

class Application {
public:
    Application(int argc, char* argv[]);
    ~Application();
    void run();

private:
    // Constants
    static constexpr float HOT_RELOAD_INTERVAL = 1.0f;

    // Initialization steps
    void init_window();
    void recreate_framebuffer(); // Renamed from create_framebuffer
    void create_screen_quad();
    void setup_gpu_compute();
    void query_uniform_locations();

    // Main loop functions
    void process_input();
    void update(float delta_time);
    void render();
    
    // Hot-reloading method
    void check_for_config_updates();

    // GPU fractal generation
    void generate_fractal_gpu(const std::vector<Transform>& frame_transforms);

    // --- Callbacks ---
    // Static callback passed to GLFW
    static void framebuffer_size_callback(GLFWwindow* window, int width, int height);
    // Member function to handle the resize logic
    void on_window_resize(int width, int height);

    // Member variables
    GLFWwindow* m_window = nullptr;
    Config m_config;
    std::string m_config_path;

    // Hot-reloading state
    float m_hot_reload_check_timer = 0.0f;
    std::filesystem::file_time_type m_last_config_write_time;

    // OpenGL handles
    GLuint m_fbo = 0, m_fbo_texture = 0, m_quad_vao = 0, m_quad_vbo = 0;
    GLuint m_accumulation_fbo = 0, m_accumulation_texture = 0;
    
    GLuint m_compute_shader_program = 0, m_point_shader_program = 0, m_quad_shader_program = 0;
    GLuint m_fade_shader_program = 0; // Add this line
    GLuint m_transforms_ssbo = 0, m_points_ssbo = 0;
    GLuint m_point_render_vao = 0;
    
    // Uniform locations
    GLint m_proj_loc;
    GLint m_res_loc;
    GLint m_brightness_loc;
    GLint m_contrast_loc;
    GLint m_gamma_loc;

    GLint m_accumulation_sampler_loc;
    GLint m_blend_factor_loc;

    GLint m_persistence_loc; // Add this line
    GLint m_num_transforms_loc;
    GLint m_total_points_loc;
    GLint m_seed_loc;

    // State management
    std::vector<Transform> m_previous_transforms;
    std::vector<Transform> m_target_transforms;
    int m_current_state_index = 0;
    int m_animation_direction = 1;
    float m_interpolation_alpha = 1.0f;
    double m_last_frame_time = 0.0;
    
    // Window dimensions
    unsigned int m_width = 0;
    unsigned int m_height = 0;

    // Random number generator
    std::random_device m_rd;
    std::mt19937 m_rd_generator;
};
