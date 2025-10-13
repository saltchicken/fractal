#include "Application.h"
#include <iostream>
#include <map>
#include <algorithm>
#include <cmath>
#include <GLFW/glfw3.h>
#include <gtc/matrix_transform.hpp>
#include <gtc/type_ptr.hpp>
#include <sstream>

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
    glDeleteProgram(m_compute_shader_program);
    if (m_window) {
        glfwDestroyWindow(m_window);
    }
    glfwTerminate();
}

void Application::run() {
    // --- Load Configuration ---
    if (!m_config.load("config.ini") || m_config.states.empty()) {
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
    m_compute_shader_program = create_compute_shader_program_from_file("shaders/comp/fractal.comp");
    
    query_uniform_locations(); // Query locations after creating shaders
    
    create_framebuffer();
    create_screen_quad();
    setup_gpu_compute();
    
    // --- Initialize Animation State ---
    m_target_transforms = m_config.states[0];
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
    if (m_config.states.size() > 1) {
        m_interpolation_alpha += delta_time / m_config.interpolation_duration;
        if (m_interpolation_alpha >= 1.0f) {
            m_previous_transforms = m_config.states[m_current_state_index];
            
            if (m_config.animation_mode == PING_PONG) {
                int next_state_index = m_current_state_index + m_animation_direction;
                if (next_state_index >= m_config.states.size() || next_state_index < 0) {
                    m_animation_direction *= -1;
                    next_state_index = m_current_state_index + m_animation_direction;
                }
                m_current_state_index = next_state_index;
            } else if (m_config.animation_mode == LOOP) {
                m_current_state_index = (m_current_state_index + 1) % m_config.states.size();
            } else { // RANDOM
                if (m_config.states.size() > 1) {
                    std::uniform_int_distribution<int> dist(0, m_config.states.size() - 1);
                    int next_state_index = m_current_state_index;
                    while (next_state_index == m_current_state_index) {
                        next_state_index = dist(m_rd_generator);
                    }
                    m_current_state_index = next_state_index;
                }
            }
            
            m_target_transforms = m_config.states[m_current_state_index];
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
            if (new_config.load("config.ini") && !new_config.states.empty()) {
                m_config = new_config; // Replace the old config with the new one
                // Gracefully reset the animation
                m_current_state_index = std::min(m_current_state_index, (int)m_config.states.size() - 1);
                m_current_state_index = std::max(0, m_current_state_index);
                m_target_transforms = m_config.states[m_current_state_index];
                m_previous_transforms = m_target_transforms;
                m_interpolation_alpha = 1.0f;
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
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_config.width, m_config.height);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_point_shader_program);
    glm::mat4 projection = glm::ortho(-2.0f, 2.0f, -2.0f, 2.0f, -1.0f, 1.0f);
    glUniformMatrix4fv(m_proj_loc, 1, GL_FALSE, glm::value_ptr(projection));
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    
    glBindVertexArray(m_point_render_vao);
    glDrawArrays(GL_POINTS, 0, m_config.total_points);
    
    glDisable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_quad_shader_program);
    glUniform2f(m_res_loc, (float)m_config.width, (float)m_config.height);
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
    m_window = glfwCreateWindow(m_config.width, m_config.height, "GPU Fractal Flame", NULL, NULL);
    if (!m_window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return;
    }
    
    glfwMakeContextCurrent(m_window);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        return;
    }
    
    glEnable(GL_PROGRAM_POINT_SIZE);
}

void Application::create_framebuffer() {
    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glGenTextures(1, &m_fbo_texture);
    glBindTexture(GL_TEXTURE_2D, m_fbo_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, m_config.width, m_config.height, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_fbo_texture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "ERROR::FRAMEBUFFER:: Framebuffer is not complete!" << std::endl;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Application::create_screen_quad() {
    float quadVertices[] = { -1.0f, 1.0f, 0.0f, 1.0f, -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, -1.0f, 1.0f, 0.0f, -1.0f, 1.0f, 0.0f, 1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f };
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
    
    glDeleteBuffers(1, &m_points_ssbo); // Delete old buffer before creating new one
    glGenBuffers(1, &m_points_ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_points_ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER, m_config.total_points * sizeof(Point), nullptr, GL_STATIC_DRAW);
}

void Application::query_uniform_locations() {
    // Graphics program uniforms
    m_proj_loc = glGetUniformLocation(m_point_shader_program, "projection");
    m_res_loc = glGetUniformLocation(m_quad_shader_program, "u_resolution");
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
    
    glUniform1ui(m_num_transforms_loc, frame_transforms.size());
    glUniform1ui(m_total_points_loc, m_config.total_points);
    
    unsigned int current_seed = (m_config.fractal_seed == 0) ? m_rd() : m_config.fractal_seed;
    
    glUniform1ui(m_seed_loc, current_seed);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_transforms_ssbo);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_points_ssbo);
    const unsigned int WORKGROUP_SIZE = 256;
    GLuint num_groups = (m_config.total_points + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
    glDispatchCompute(num_groups, 1, 1);
    
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}
