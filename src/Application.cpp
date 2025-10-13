#include "Application.h"
#include <iostream>
#include <map>
#include <algorithm>
#include <cmath>
#include <GLFW/glfw3.h>
#include <gtc/matrix_transform.hpp>
#include <gtc/type_ptr.hpp>
#include <sstream>

// --- Callback Implementations ---
// Static function passed to GLFW, which calls the member function
void Application::framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
    if (app) {
        app->on_window_resize(width, height);
    }
}

// Member function that contains the actual resizing logic
void Application::on_window_resize(int width, int height) {
    // Avoid resizing to 0x0 which can happen on minimization
    if (width == 0 || height == 0) {
        return;
    }

    m_width = static_cast<unsigned int>(width);
    m_height = static_cast<unsigned int>(height);

    // Update the OpenGL viewport and recreate the framebuffer
    glViewport(0, 0, m_width, m_height);
    recreate_framebuffer();
    
    std::cout << "Window resized to " << m_width << "x" << m_height << std::endl;
}

Application::Application() : m_rd_generator(m_rd()) {}

Application::~Application() {
    glDeleteVertexArrays(1, &m_point_render_vao);
    glDeleteVertexArrays(1, &m_quad_vao);
    glDeleteBuffers(1, &m_quad_vbo);
    glDeleteBuffers(1, &m_transforms_ssbo);
    glDeleteBuffers(1, &m_points_ssbo);
    glDeleteFramebuffers(1, &m_fbo);
    glDeleteTextures(1, &m_fbo_texture);
    glDeleteProgram(m_point_shader_program);
    glDeleteProgram(m_quad_shader_program);
    glDeleteProgram(m_fade_shader_program); // Cleanup for the new shader
    glDeleteProgram(m_compute_shader_program);

    if (m_window) {
        glfwDestroyWindow(m_window);
    }
    glfwTerminate();
}

void Application::run() {
    // --- Load Configuration ---
    if (!m_config.load("config.ini") || m_config.getStates().empty()) {
        std::cerr << "Config load failed or no states found. Please check config.ini" << std::endl;
        return;
    }
    m_last_config_write_time = std::filesystem::last_write_time("config.ini");
    m_hot_reload_check_timer = HOT_RELOAD_INTERVAL;
    
    // --- Initialization ---
    init_window();
    if (!m_window) return;

    m_point_shader_program = create_shader_program_from_files("shaders/vert/point.vert", "shaders/frag/point.frag");
    m_quad_shader_program = create_shader_program_from_files("shaders/vert/quad.vert", "shaders/frag/quad.frag");
    m_fade_shader_program = create_shader_program_from_files("shaders/vert/quad.vert", "shaders/frag/fade.frag");
    m_compute_shader_program = create_compute_shader_program_from_file("shaders/comp/fractal.comp");
    
    query_uniform_locations(); // Query locations after creating shaders
    
    recreate_framebuffer(); // Changed from create_framebuffer
    create_screen_quad();
    setup_gpu_compute();
    
    // --- Initialize Animation State ---
    m_target_transforms = m_config.getStates()[0];
    m_previous_transforms = m_target_transforms;
    
    glGenVertexArrays(1, &m_point_render_vao);
    m_last_frame_time = glfwGetTime();
    
    // --- Main Loop ---
    while (!glfwWindowShouldClose(m_window)) {
        double current_time = glfwGetTime();
        float delta_time = static_cast<float>(current_time - m_last_frame_time);
        m_last_frame_time = current_time;

        process_input();
        update(delta_time);
        render();
    }
}

void Application::process_input() {
    glfwPollEvents();
    if (glfwGetKey(m_window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(m_window, true);
    }
}

void Application::update(float delta_time) {
    // Handle hot-reloading check
    m_hot_reload_check_timer -= delta_time;
    if (m_hot_reload_check_timer <= 0.0f) {
        check_for_config_updates();
        m_hot_reload_check_timer = HOT_RELOAD_INTERVAL; // Reset timer
    }
    
    // Animation interpolation logic
    if (m_config.getStates().size() > 1) {
        m_interpolation_alpha += delta_time / m_config.getInterpolationDuration();
        if (m_interpolation_alpha >= 1.0f) {
            m_previous_transforms = m_config.getStates()[m_current_state_index];
            
            if (m_config.getAnimationMode() == PING_PONG) {
                int next_state_index = m_current_state_index + m_animation_direction;
                if (next_state_index >= (int)m_config.getStates().size() || next_state_index < 0) {
                    m_animation_direction *= -1;
                    next_state_index = m_current_state_index + m_animation_direction;
                }
                m_current_state_index = next_state_index;
            } else if (m_config.getAnimationMode() == LOOP) {
                m_current_state_index = (m_current_state_index + 1) % m_config.getStates().size();
            } else { // RANDOM
                if (m_config.getStates().size() > 1) {
                    std::uniform_int_distribution<int> dist(0, (int)m_config.getStates().size() - 1);
                    int next_state_index = m_current_state_index;
                    while (next_state_index == m_current_state_index) {
                        next_state_index = dist(m_rd_generator);
                    }
                    m_current_state_index = next_state_index;
                }
            }
            
            m_target_transforms = m_config.getStates()[m_current_state_index];
            m_interpolation_alpha = fmod(m_interpolation_alpha, 1.0f);
            std::cout << "Animating to state " << (m_current_state_index + 1) << "..." << std::endl;
        }
    } else {
        m_interpolation_alpha = 1.0f;
    }
}

void Application::check_for_config_updates() {
    try {
        auto current_write_time = std::filesystem::last_write_time("config.ini");
        if (current_write_time > m_last_config_write_time) {
            m_last_config_write_time = current_write_time;
            std::cout << "Config file changed, attempting to reload..." << std::endl;

            Config new_config;
            if (new_config.load("config.ini") && !new_config.getStates().empty()) {
                m_config = new_config; // Replace the old config with the new one
                // Gracefully reset the animation
                m_current_state_index = std::min(m_current_state_index, (int)m_config.getStates().size() - 1);
                m_current_state_index = std::max(0, m_current_state_index);
                m_target_transforms = m_config.getStates()[m_current_state_index];
                m_previous_transforms = m_target_transforms;
                m_interpolation_alpha = 1.0f;

                // If window dimensions changed in config, resize the window
                if (m_width != m_config.getWidth() || m_height != m_config.getHeight()) {
                    glfwSetWindowSize(m_window, m_config.getWidth(), m_config.getHeight());
                }
                // Re-initialize GPU buffers in case TotalPoints changed
                setup_gpu_compute(); 
                std::cout << "Successfully reloaded config.ini!" << std::endl;
            } else {
                std::cerr << "Failed to reload config.ini, keeping old settings." << std::endl;
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error checking config file: " << e.what() << std::endl;
    }
}

void Application::render() {
    std::vector<Transform> interpolated_transforms;
    size_t num_target = m_target_transforms.size();
    size_t num_previous = m_previous_transforms.size();
    size_t render_list_size = std::max(num_target, num_previous);
    interpolated_transforms.reserve(render_list_size);

    for (size_t i = 0; i < render_list_size; ++i) {
        bool is_appearing = (i >= num_previous);
        bool is_disappearing = (i >= num_target);
        Transform prev, target;

        if (is_disappearing) {
            prev = m_previous_transforms[i];
            target = m_previous_transforms[i];
            target.color.a = 0.0f;
        } else if (is_appearing) {
            target = m_target_transforms[i];
            prev = m_target_transforms[i];
            prev.color.a = 0.0f;
        } else {
            prev = m_previous_transforms[i];
            target = m_target_transforms[i];
        }
        Transform interpolated;
        interpolated.params1 = glm::mix(prev.params1, target.params1, m_interpolation_alpha);
        interpolated.params2 = glm::mix(prev.params2, target.params2, m_interpolation_alpha);
        interpolated.color = glm::mix(prev.color, target.color, m_interpolation_alpha);
        interpolated.variation = (m_interpolation_alpha < 0.5f) ? prev.variation : target.variation;
        
        interpolated_transforms.push_back(interpolated);
    }

    if (!interpolated_transforms.empty()) {
        generate_fractal_gpu(interpolated_transforms);
    }

    // --- Render to Framebuffer ---
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    // --- 1. Fade Pass ---
    // Draw a semi-transparent quad over the entire FBO to fade the previous frame.
    glUseProgram(m_fade_shader_program);
    glUniform1f(m_persistence_loc, m_config.getPersistence());
    glEnable(GL_BLEND);
    // This blend function multiplies the destination (the old frame) by the
    // inverse of the source alpha.
    glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(m_quad_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // --- 2. Point Pass ---
    // Now, additively blend the new points on top of the faded image.
    glUseProgram(m_point_shader_program);
    
    float zoom = m_config.getCameraZoom();
    float x_offset = m_config.getCameraX();
    float y_offset = m_config.getCameraY();
    float aspect_ratio = (float)m_width / (float)m_height;
    float half_height = 2.0f / (zoom < 1e-6f ? 1e-6f : zoom);
    float half_width = half_height * aspect_ratio;
    glm::mat4 projection = glm::ortho(
        -half_width - x_offset, half_width - x_offset,
        -half_height - y_offset, half_height - y_offset,
        -1.0f, 1.0f
    );
    glUniformMatrix4fv(m_proj_loc, 1, GL_FALSE, glm::value_ptr(projection));
    
    // Switch to additive blending for the points
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    
    glBindVertexArray(m_point_render_vao);
    glDrawArrays(GL_POINTS, 0, (GLsizei)m_config.getTotalPoints());
    
    // --- Render to Screen ---
    glDisable(GL_BLEND); // Disable blend for the final screen quad
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(m_quad_shader_program);
    glUniform2f(m_res_loc, (float)m_width, (float)m_height);
    glUniform1f(m_brightness_loc, m_config.getBrightness());
    glUniform1f(m_contrast_loc, m_config.getContrast());
    glUniform1f(m_gamma_loc, m_config.getGamma());
    glBindVertexArray(m_quad_vao);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_fbo_texture);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glfwSwapBuffers(m_window);
}

void Application::init_window() {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);

    m_width = m_config.getWidth();
    m_height = m_config.getHeight();
    m_window = glfwCreateWindow(m_width, m_height, "GPU Fractal Flame", NULL, NULL);

    if (!m_window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return;
    }
    
    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, framebuffer_size_callback);
    glfwMakeContextCurrent(m_window);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        return;
    }
    
    glViewport(0, 0, m_width, m_height);
    
    glEnable(GL_PROGRAM_POINT_SIZE);
}

void Application::recreate_framebuffer() {
    if (m_fbo) glDeleteFramebuffers(1, &m_fbo);
    if (m_fbo_texture) glDeleteTextures(1, &m_fbo_texture);

    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    glGenTextures(1, &m_fbo_texture);
    glBindTexture(GL_TEXTURE_2D, m_fbo_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, m_width, m_height, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_fbo_texture, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "ERROR::FRAMEBUFFER:: Framebuffer is not complete!" << std::endl;
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Application::create_screen_quad() {
    float quadVertices[] = { 
        -1.0f,  1.0f, 0.0f, 1.0f,
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f 
    };
    glGenVertexArrays(1, &m_quad_vao);
    glGenBuffers(1, &m_quad_vbo);
    glBindVertexArray(m_quad_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
}

void Application::setup_gpu_compute() {
    glGenBuffers(1, &m_transforms_ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_transforms_ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER, 100 * sizeof(Transform), nullptr, GL_DYNAMIC_DRAW);
    
    if (m_points_ssbo) glDeleteBuffers(1, &m_points_ssbo);
    glGenBuffers(1, &m_points_ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_points_ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER, m_config.getTotalPoints() * sizeof(Point), nullptr, GL_STATIC_DRAW);
}

void Application::query_uniform_locations() {
    // Graphics program uniforms
    m_proj_loc = glGetUniformLocation(m_point_shader_program, "projection");
    m_res_loc = glGetUniformLocation(m_quad_shader_program, "u_resolution");
    m_brightness_loc = glGetUniformLocation(m_quad_shader_program, "u_brightness");
    m_contrast_loc = glGetUniformLocation(m_quad_shader_program, "u_contrast");
    m_gamma_loc = glGetUniformLocation(m_quad_shader_program, "u_gamma");
    m_persistence_loc = glGetUniformLocation(m_fade_shader_program, "u_persistence");

    // Compute program uniforms
    m_num_transforms_loc = glGetUniformLocation(m_compute_shader_program, "num_transforms");
    m_total_points_loc = glGetUniformLocation(m_compute_shader_program, "total_points");
    m_seed_loc = glGetUniformLocation(m_compute_shader_program, "seed");
}

void Application::generate_fractal_gpu(const std::vector<Transform>& frame_transforms) {
    if (frame_transforms.empty()) return;
    
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_transforms_ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER, frame_transforms.size() * sizeof(Transform), frame_transforms.data(), GL_DYNAMIC_DRAW);
    
    glUseProgram(m_compute_shader_program);
    
    glUniform1ui(m_num_transforms_loc, (GLuint)frame_transforms.size());
    glUniform1ui(m_total_points_loc, (GLuint)m_config.getTotalPoints());
    
    unsigned int current_seed = (m_config.getFractalSeed() == 0) ? m_rd() : m_config.getFractalSeed();
    
    glUniform1ui(m_seed_loc, current_seed);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_transforms_ssbo);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_points_ssbo);

    const unsigned int WORKGROUP_SIZE = 256;
    GLuint num_groups = (GLuint)(m_config.getTotalPoints() + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
    glDispatchCompute(num_groups, 1, 1);
    
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}
