#pragma once

#include <vector>
#include <string>
#include "Shader.h"
#include <GLFW/glfw3.h>
#include <glm.hpp>
#include <random>

// Using forward declaration for GLFWwindow
struct GLFWwindow;

// Data Structures (can be moved to a separate file later)
struct Point {
    glm::vec4 position;
    glm::vec4 color;
};

enum Variation { LINEAR, SINUSOIDAL, SPHERICAL, SWIRL, HORSESHOE };

struct Transform {
    glm::vec4 params1{};
    glm::vec4 params2{};
    glm::vec4 color{};
    glm::uvec4 variation{};
};

enum AnimationMode { PING_PONG, LOOP, RANDOM };

class Application {
public:
    Application();
    ~Application();

    // Initializes the application (window, OpenGL, etc.) and runs the main loop
    void run();

private:
    // Initialization steps
    void init_window();
    void create_framebuffer();
    void create_screen_quad();
    void setup_gpu_compute();
    bool load_config_states();

    // Main loop functions
    void process_input();
    void update(float delta_time);
    void render();

    // GPU fractal generation
    void generate_fractal_gpu(const std::vector<Transform>& frame_transforms);

    // Member variables
    GLFWwindow* m_window = nullptr;
    const std::string m_config_filename = "config.ini";

    // Application settings from config
    unsigned int m_scr_width = 1280;
    unsigned int m_scr_height = 720;
    long long m_total_points = 500000;
    float m_interpolation_duration = 2.0f;
    unsigned int m_fractal_seed = 0;
    AnimationMode m_animation_mode = PING_PONG;

    // OpenGL handles
    GLuint m_fbo, m_fbo_texture, m_quad_vao, m_quad_vbo;
    GLuint m_compute_shader_program, m_point_shader_program, m_quad_shader_program;
    GLuint m_transforms_ssbo, m_points_ssbo;
    GLuint m_point_render_vao;

    // State management
    std::vector<Transform> m_previous_transforms;
    std::vector<Transform> m_target_transforms;
    std::vector<std::vector<Transform>> m_config_states;
    int m_current_state_index = 0;
    int m_animation_direction = 1;
    float m_interpolation_alpha = 1.0f;
    double m_last_frame_time = 0.0;
    
    // Random number generator
    std::random_device m_rd;
    std::mt19937 m_rd_generator;
};
