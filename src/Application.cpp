#include "Application.h"
#include "cxxopts.hpp"
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

Application::Application(int argc, char* argv[]) : m_rd_generator(m_rd()) {
    cxxopts::Options options("FractalFlame", "A GPU-accelerated fractal flame renderer");

    options.add_options()
        ("c,config", "Path to the configuration INI file", cxxopts::value<std::string>()->default_value("config.ini"))
        ("h,help", "Print usage information");
    
    auto result = options.parse(argc, argv);

    if (result.count("help")) {
        std::cout << options.help() << std::endl;
        exit(0); // Exit cleanly after showing help
    }

    // Initialize member variable from the parsed option
    m_config_path = result["config"].as<std::string>();
    
    std::cout << "Loading configuration from: " << m_config_path << std::endl;
}

Application::~Application() {
    glDeleteVertexArrays(1, &m_point_render_vao);
    glDeleteVertexArrays(1, &m_quad_vao);
    glDeleteBuffers(1, &m_quad_vbo);
    glDeleteBuffers(1, &m_transforms_ssbo);
    glDeleteBuffers(1, &m_points_ssbo);
    glDeleteFramebuffers(1, &m_fbo);
    glDeleteTextures(1, &m_fbo_texture);
    glDeleteFramebuffers(1, &m_accumulation_fbo);
    glDeleteTextures(1, &m_accumulation_texture);
    
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
    if (!m_config.load(m_config_path) || m_config.getStates().empty()) {
        std::cerr << "Config load failed or no states found. Please check " << m_config_path << std::endl;
        return;
    }
    m_last_config_write_time = std::filesystem::last_write_time(m_config_path);
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
        auto current_write_time = std::filesystem::last_write_time(m_config_path);
        if (current_write_time > m_last_config_write_time) {
            m_last_config_write_time = current_write_time;
            std::cout << m_config_path << " changed, attempting to reload..." << std::endl;
            Config new_config;
            if (new_config.load(m_config_path) && !new_config.getStates().empty()) {
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
                std::cout << "Successfully reloaded " << m_config_path << "!" << std::endl;
            } else {
                std::cerr << "Failed to reload " << m_config_path << ", keeping old settings." << std::endl;
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error checking config file " << m_config_path << ": " << e.what() << std::endl;
    }
}

void Application::render() {
    // --- PART 0: Calculate Interpolated Transforms ---
    // This part calculates the current state of the fractal transforms based on animation progress.
    // It remains unchanged from before.
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
    // Generate the point cloud for the current frame using the compute shader
    if (!interpolated_transforms.empty()) {
        generate_fractal_gpu(interpolated_transforms);
    }
    // --- PART 1: Render Raw Points with Motion Blur ---
    // We bind our primary framebuffer (m_fbo) to draw the new points into it.
    // This pass creates the motion blur trails.
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glEnable(GL_BLEND);
    // A. Fade Pass: Draw a semi-transparent black quad over the last frame's content in this FBO.
    glUseProgram(m_fade_shader_program);
    glUniform1f(m_persistence_loc, m_config.getPersistence());
    glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_ALPHA); // Correct blend function for persistence
    glBindVertexArray(m_quad_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    // B. Point Pass: Additively draw the new points on top of the faded image.
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
    glBlendFunc(GL_SRC_ALPHA, GL_ONE); // Switch to additive blending
    glBindVertexArray(m_point_render_vao);
    glDrawArrays(GL_POINTS, 0, (GLsizei)m_config.getTotalPoints());
    // --- PART 2: Accumulation Pass for Denoising ---
    // We bind the accumulation framebuffer to blend the newly rendered points (with motion blur)
    // into our historical average, which smooths out the grain.
    glBindFramebuffer(GL_FRAMEBUFFER, m_accumulation_fbo);
    glUseProgram(m_quad_shader_program);
    glDisable(GL_BLEND); // Blending is done inside the shader with mix()
    // Set the blend factor to tell the shader we are in accumulation mode
    glUniform1f(m_blend_factor_loc, m_config.getDenoiseFactor());
    // Bind the texture from the primary FBO (current frame) to texture unit 0
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_fbo_texture);
    glUniform1i(glGetUniformLocation(m_quad_shader_program, "screenTexture"), 0);
    // Bind the accumulation texture (historical frames) to texture unit 1
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_accumulation_texture);
    glUniform1i(m_accumulation_sampler_loc, 1);
    // Draw the quad. The shader will read from both textures and write the blended result
    // back into the accumulation texture attached to this FBO.
    glBindVertexArray(m_quad_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    // --- PART 3: Final Display Pass ---
    // Unbind any framebuffer, which means we are now drawing to the screen.
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    // Use the same quad shader, but switch it to display mode
    glUseProgram(m_quad_shader_program);
    glUniform1f(m_blend_factor_loc, 0.0f); // A blend factor of 0 triggers the final post-fx path
    // Set all post-processing uniforms
    glUniform2f(m_res_loc, (float)m_width, (float)m_height);
    glUniform1f(m_brightness_loc, m_config.getBrightness());
    glUniform1f(m_contrast_loc, m_config.getContrast());
    glUniform1f(m_gamma_loc, m_config.getGamma());
    // Bind the final, denoised image from the accumulation texture to be processed and displayed
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_accumulation_texture);
    glUniform1i(glGetUniformLocation(m_quad_shader_program, "screenTexture"), 0);
    // Draw the final image to the screen
    glBindVertexArray(m_quad_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    // Swap the front and back buffers to display the rendered frame
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
    // This function now creates/recreates BOTH framebuffers
    if (m_fbo) glDeleteFramebuffers(1, &m_fbo);
    if (m_fbo_texture) glDeleteTextures(1, &m_fbo_texture);
    if (m_accumulation_fbo) glDeleteFramebuffers(1, &m_accumulation_fbo);
    if (m_accumulation_texture) glDeleteTextures(1, &m_accumulation_texture);
    // --- Main FBO (for raw points) ---
    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glGenTextures(1, &m_fbo_texture);
    glBindTexture(GL_TEXTURE_2D, m_fbo_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, m_width, m_height, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_fbo_texture, 0);
    // --- Accumulation FBO (for denoised image) ---
    glGenFramebuffers(1, &m_accumulation_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_accumulation_fbo);
    glGenTextures(1, &m_accumulation_texture);
    glBindTexture(GL_TEXTURE_2D, m_accumulation_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, m_width, m_height, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_accumulation_texture, 0);
    // Clear the accumulation buffer initially
    glClear(GL_COLOR_BUFFER_BIT);
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
    m_accumulation_sampler_loc = glGetUniformLocation(m_quad_shader_program, "accumulationTexture");
    m_blend_factor_loc = glGetUniformLocation(m_quad_shader_program, "u_blend_factor");
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
