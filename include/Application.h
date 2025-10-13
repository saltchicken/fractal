#pragma once

#include "Shader.h"
#include "Config.h" 
#include <string>
#include <vector>
#include <random>

// Using forward declaration for GLFWwindow
struct GLFWwindow;

// RE-ADD THIS STRUCT
struct Point {
    glm::vec4 position;
    glm::vec4 color;
};

class Application {
public:
    Application();
    ~Application();

    void run();

private:
    // Initialization steps
    void init_window();
    void create_framebuffer();
    void create_screen_quad();
    void setup_gpu_compute();
    
    // Main loop functions
    void process_input();
    void update(float delta_time);
    void render();

    // GPU fractal generation
    void generate_fractal_gpu(const std::vector<Transform>& frame_transforms);

    // Member variables
    GLFWwindow* m_window = nullptr;
    Config m_config;

    // OpenGL handles
    GLuint m_fbo, m_fbo_texture, m_quad_vao, m_quad_vbo;
    GLuint m_compute_shader_program, m_point_shader_program, m_quad_shader_program;
    GLuint m_transforms_ssbo, m_points_ssbo;
    GLuint m_point_render_vao;

    // State management
    std::vector<Transform> m_previous_transforms;
    std::vector<Transform> m_target_transforms;
    int m_current_state_index = 0;
    int m_animation_direction = 1;
    float m_interpolation_alpha = 1.0f;
    double m_last_frame_time = 0.0;
    
    // Random number generator
    std::random_device m_rd;
    std::mt19937 m_rd_generator;
};
