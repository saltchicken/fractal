#include "Application.h"
#include "ini.h"
#include <iostream>
#include <map>
#include <algorithm>
#include <cmath>
#include <glad/glad.h>
#include <gtc/matrix_transform.hpp>
#include <gtc/type_ptr.hpp>
#include <sstream>

// Maps for config parsing
const std::map<std::string, Variation> variation_map = {
    {"LINEAR", LINEAR}, {"SINUSOIDAL", SINUSOIDAL}, {"SPHERICAL", SPHERICAL},
    {"SWIRL", SWIRL}, {"HORSESHOE", HORSESHOE}
};

Application::Application() : m_rd_generator(m_rd()) {}

Application::~Application() {
    // Cleanup OpenGL resources
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

    // Terminate GLFW
    if (m_window) {
        glfwDestroyWindow(m_window);
    }
    glfwTerminate();
}

void Application::run() {
    // --- Initialization ---
    if (!load_config_states()) {
        std::cerr << "Initial config load failed. Please check " << m_config_filename << std::endl;
        return;
    }
    
    init_window();
    if (!m_window) return;

    // --- Load Shaders and Setup GPU Resources ---
    m_point_shader_program = create_shader_program_from_files("shaders/vert/point.vert", "shaders/frag/point.frag");
    m_quad_shader_program = create_shader_program_from_files("shaders/vert/quad.vert", "shaders/frag/quad.frag");
    m_compute_shader_program = create_compute_shader_program_from_file("shaders/comp/fractal.comp");
    
    create_framebuffer();
    create_screen_quad();
    setup_gpu_compute();

    // This is a bit redundant but ensures GPU buffers are correctly sized after config load
    if (!load_config_states() || m_config_states.empty()) {
        std::cerr << "Config load failed or no states found after GL init." << std::endl;
        return;
    }
    
    m_target_transforms = m_config_states[0];
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
    if (m_config_states.size() > 1) {
        m_interpolation_alpha += delta_time / m_interpolation_duration;
        if (m_interpolation_alpha >= 1.0f) {
            m_previous_transforms = m_config_states[m_current_state_index];
            
            if (m_animation_mode == PING_PONG) {
                int next_state_index = m_current_state_index + m_animation_direction;
                if (next_state_index >= m_config_states.size() || next_state_index < 0) {
                    m_animation_direction *= -1;
                    next_state_index = m_current_state_index + m_animation_direction;
                }
                m_current_state_index = next_state_index;
            } else if (m_animation_mode == LOOP) {
                m_current_state_index = (m_current_state_index + 1) % m_config_states.size();
            } else { // RANDOM
                if (m_config_states.size() > 1) {
                    std::uniform_int_distribution<int> dist(0, m_config_states.size() - 1);
                    int next_state_index = m_current_state_index;
                    while (next_state_index == m_current_state_index) {
                        next_state_index = dist(m_rd_generator);
                    }
                    m_current_state_index = next_state_index;
                }
            }
            
            m_target_transforms = m_config_states[m_current_state_index];
            m_interpolation_alpha = fmod(m_interpolation_alpha, 1.0f);
            std::cout << "Animating to state " << (m_current_state_index + 1) << "..." << std::endl;
        }
    } else {
        m_interpolation_alpha = 1.0f;
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

    // --- Render Pass: Draw points to FBO ---
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_scr_width, m_scr_height);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_point_shader_program);
    glm::mat4 projection = glm::ortho(-2.0f, 2.0f, -2.0f, 2.0f, -1.0f, 1.0f);
    glUniformMatrix4fv(glGetUniformLocation(m_point_shader_program, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    
    glBindVertexArray(m_point_render_vao);
    glDrawArrays(GL_POINTS, 0, m_total_points);
    
    glDisable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // --- Post-Processing Pass: Draw FBO to screen quad ---
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_quad_shader_program);
    glUniform2f(glGetUniformLocation(m_quad_shader_program, "u_resolution"), (float)m_scr_width, (float)m_scr_height);
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

    m_window = glfwCreateWindow(m_scr_width, m_scr_height, "GPU Fractal Flame", NULL, NULL);
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
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, m_scr_width, m_scr_height, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_fbo_texture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "ERROR::FRAMEBUFFER:: Framebuffer is not complete!" << std::endl;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Application::create_screen_quad() {
    float quadVertices[] = {
        // positions   // texCoords
        -1.0f,  1.0f,  0.0f, 1.0f,
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,

        -1.0f,  1.0f,  0.0f, 1.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f
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
    // Allocate a generous initial size
    glBufferData(GL_SHADER_STORAGE_BUFFER, 100 * sizeof(Transform), nullptr, GL_DYNAMIC_DRAW);
    
    glGenBuffers(1, &m_points_ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_points_ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER, m_total_points * sizeof(Point), nullptr, GL_STATIC_DRAW);
}

void Application::generate_fractal_gpu(const std::vector<Transform>& frame_transforms) {
    if (frame_transforms.empty()) return;

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_transforms_ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER, frame_transforms.size() * sizeof(Transform), frame_transforms.data(), GL_DYNAMIC_DRAW);

    glUseProgram(m_compute_shader_program);
    glUniform1ui(glGetUniformLocation(m_compute_shader_program, "num_transforms"), frame_transforms.size());
    glUniform1ui(glGetUniformLocation(m_compute_shader_program, "total_points"), m_total_points);
    
    unsigned int current_seed = (m_fractal_seed == 0) ? m_rd() : m_fractal_seed;
    glUniform1ui(glGetUniformLocation(m_compute_shader_program, "seed"), current_seed);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_transforms_ssbo);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_points_ssbo);

    const unsigned int WORKGROUP_SIZE = 256;
    GLuint num_groups = (m_total_points + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
    glDispatchCompute(num_groups, 1, 1);
    
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

bool Application::load_config_states() {
    m_config_states.clear();
    simpleini::INIReader reader;
    if (!reader.load(m_config_filename)) {
        std::cerr << "Failed to load " << m_config_filename << std::endl;
        return false;
    }

    const auto& config_data = reader.get_data();
    std::map<int, std::vector<Transform>> state_map;

    try {
        if (config_data.count("Settings")) {
            const auto& settings = config_data.at("Settings");
            if (settings.count("Width")) m_scr_width = std::stoi(settings.at("Width"));
            if (settings.count("Height")) m_scr_height = std::stoi(settings.at("Height"));
            if (settings.count("InterpolationDuration")) m_interpolation_duration = std::stof(settings.at("InterpolationDuration"));
            if (settings.count("TotalPoints")) m_total_points = std::stoll(settings.at("TotalPoints"));
            if (settings.count("Seed")) m_fractal_seed = std::stoul(settings.at("Seed"));
            
            if (settings.count("AnimationMode")) {
                std::string mode_str = settings.at("AnimationMode");
                std::transform(mode_str.begin(), mode_str.end(), mode_str.begin(), ::tolower);
                if (mode_str == "loop") m_animation_mode = LOOP;
                else if (mode_str == "random") m_animation_mode = RANDOM;
                else m_animation_mode = PING_PONG;
            }
        }

        for (const auto& pair : config_data) {
            const std::string& section_name = pair.first;
            if (section_name.rfind("State.", 0) == 0) {
                std::string temp = section_name.substr(6);
                size_t dot_pos = temp.find('.');
                if (dot_pos == std::string::npos) continue;
                int state_num = std::stoi(temp.substr(0, dot_pos));

                const auto& section = pair.second;
                Transform t;
                float a=0,b=0,c=0,d=0,e=0,f=0;
                if (section.count("a")) a = std::stof(section.at("a"));
                if (section.count("b")) b = std::stof(section.at("b"));
                if (section.count("c")) c = std::stof(section.at("c"));
                if (section.count("d")) d = std::stof(section.at("d"));
                if (section.count("e")) e = std::stof(section.at("e"));
                if (section.count("f")) f = std::stof(section.at("f"));
                t.params1 = glm::vec4(a,b,c,d);
                t.params2 = glm::vec4(e,f,0,0);

                if (section.count("color")) {
                    glm::vec3 color_vec;
                    std::stringstream ss(section.at("color"));
                    ss >> color_vec.r; ss.ignore(); ss >> color_vec.g; ss.ignore(); ss >> color_vec.b;
                    t.color = glm::vec4(color_vec, 0.15f);
                }
                if (section.count("variation")) {
                    std::string var_str = section.at("variation");
                    if (variation_map.count(var_str)) { t.variation.x = variation_map.at(var_str); }
                }
                state_map[state_num].push_back(t);
            }
        }
        if (!state_map.empty()) {
            int max_state = state_map.rbegin()->first;
            m_config_states.resize(max_state);
            for (const auto& pair : state_map) {
                if(pair.first > 0) m_config_states[pair.first - 1] = pair.second;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error parsing config file: " << e.what() << std::endl;
        return false;
    }
    return true;
}
