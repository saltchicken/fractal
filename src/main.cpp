// src/main.cpp
// A single-pass, auto-reloading fractal flame renderer using GPU compute.
#include <iostream>
#include <vector>
#include <random>
#include <string>
#include <map>
#include <sstream>
#include <filesystem>
#include <chrono>
#include <algorithm> // Required for std::max/min
#include <cmath>     // Required for fmod
#include "ini.h"
#include "Shader.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm.hpp>
#include <gtc/matrix_transform.hpp>
#include <gtc/type_ptr.hpp>

// --- Configuration ---
const std::string CONFIG_FILENAME = "config.ini";
unsigned int SCR_WIDTH = 1280;
unsigned int SCR_HEIGHT = 720;
long long TOTAL_POINTS = 500000;
const unsigned int WORKGROUP_SIZE = 256;

// --- Data Structures (std430 aligned) ---
// Using vec4 for all members simplifies memory alignment between C++ and GLSL
struct Point {
    glm::vec4 position; // .xy = position, .zw unused
    glm::vec4 color;
};

enum Variation { LINEAR, SINUSOIDAL, SPHERICAL, SWIRL, HORSESHOE };
const std::map<std::string, Variation> variation_map = {
    {"LINEAR", LINEAR}, {"SINUSOIDAL", SINUSOIDAL}, {"SPHERICAL", SPHERICAL},
    {"SWIRL", SWIRL}, {"HORSESHOE", HORSESHOE}
};

struct Transform {
    glm::vec4 params1{}; // x=a, y=b, z=c, w=d
    glm::vec4 params2{}; // x=e, y=f
    glm::vec4 color{};
    glm::uvec4 variation{}; // .x = variation_enum
};

enum AnimationMode { PING_PONG, LOOP, RANDOM };
AnimationMode animation_mode = PING_PONG; // Default to ping-pong

// --- Global State ---
// State management for interpolation and animation
std::vector<Transform> previous_transforms;
std::vector<Transform> target_transforms;
std::vector<std::vector<Transform>> config_states; // Holds all presets from config.ini
int current_state_index = 0;
int animation_direction = 1; // 1 for forward, -1 for backward
float interpolation_alpha = 1.0f; // 0.0 = previous, 1.0 = target
float INTERPOLATION_DURATION = 2.0f; // seconds

// NEW: Seed for the fractal. 0 = random, any other value is fixed.
unsigned int fractal_seed = 0;
std::random_device rd;
// Add a generator instance for use in C++
std::mt19937 rd_generator(rd()); 

GLuint fbo, fbo_texture, quad_vao, quad_vbo;
GLuint computeShaderProgram, pointShaderProgram;
GLuint transforms_ssbo, points_ssbo;
double last_frame_time = 0.0;

// --- Function Prototypes ---
void generate_fractal_gpu(const std::vector<Transform>& frame_transforms);
GLFWwindow* init_window();
void create_framebuffer();
void create_screen_quad();
void setup_gpu_compute();
bool load_config_states(const std::string& filename, std::vector<std::vector<Transform>>& out_states);

int main() {
    GLFWwindow* window = init_window();
    if (!window) return -1;
    
    pointShaderProgram = create_shader_program_from_files("shaders/point.vert", "shaders/point.frag");
    unsigned int quadShaderProgram = create_shader_program_from_files("shaders/quad.vert", "shaders/quad.frag");
    computeShaderProgram = create_compute_shader_program_from_file("shaders/fractal.comp");

    create_framebuffer();
    create_screen_quad();
    setup_gpu_compute();
    
    // Load config states again now that GL is initialized. This is slightly redundant but safe.
    if (!load_config_states(CONFIG_FILENAME, config_states) || config_states.empty()) {
        std::cerr << "Config load failed or no states found. Please check " << CONFIG_FILENAME << std::endl;
        glfwTerminate();
        return -1;
    }
    
    target_transforms = config_states[0];
    previous_transforms = target_transforms; // Start with both states identical
    
    GLuint vao;
    glGenVertexArrays(1, &vao);
    last_frame_time = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        // --- Delta Time Calculation ---
        double current_time = glfwGetTime();
        float delta_time = static_cast<float>(current_time - last_frame_time);
        last_frame_time = current_time;
        glfwPollEvents();

        // --- Interpolation & State Change Logic ---
        // This logic handles continuous animation from one state to the next without pausing.
        if (config_states.size() > 1) {
            interpolation_alpha += delta_time / INTERPOLATION_DURATION;

            // When a transition completes, immediately start the next one.
            if (interpolation_alpha >= 1.0f) {
                // The target state of the just-finished transition becomes the starting point.
                previous_transforms = config_states[current_state_index];

                // Determine the next state based on the current animation mode.
                if (animation_mode == PING_PONG) {
                    int next_state_index = current_state_index + animation_direction;
                    if (next_state_index >= config_states.size() || next_state_index < 0) {
                        animation_direction *= -1; 
                        next_state_index = current_state_index + animation_direction;
                    }
                    current_state_index = next_state_index;
                } else if (animation_mode == LOOP) {
                    current_state_index = (current_state_index + 1) % config_states.size();
                } else { // RANDOM mode
                    if (config_states.size() > 1) {
                        std::uniform_int_distribution<int> dist(0, config_states.size() - 1);
                        int next_state_index = current_state_index;
                        // Ensure we don't pick the same state twice in a row.
                        while (next_state_index == current_state_index) {
                            next_state_index = dist(rd_generator);
                        }
                        current_state_index = next_state_index;
                    }
                }
                
                // Set the new target state and carry over the remainder time for a smooth transition.
                target_transforms = config_states[current_state_index];
                interpolation_alpha = fmod(interpolation_alpha, 1.0f);
                std::cout << "Animating to state " << (current_state_index + 1) << "..." << std::endl;
            }
        } else {
             interpolation_alpha = 1.0f; // If only one state, stay at 100%
        }
        
        std::vector<Transform> interpolated_transforms;
        size_t num_target = target_transforms.size();
        size_t num_previous = previous_transforms.size();
        size_t render_list_size = std::max(num_target, num_previous);
        interpolated_transforms.reserve(render_list_size);

        for (size_t i = 0; i < render_list_size; ++i) {
            bool is_appearing = (i >= num_previous);
            bool is_disappearing = (i >= num_target);
            Transform prev, target;

            if (is_disappearing) {
                prev = previous_transforms[i];
                target = previous_transforms[i]; 
                target.color.a = 0.0f;
            } else if (is_appearing) {
                target = target_transforms[i];
                prev = target_transforms[i]; 
                prev.color.a = 0.0f;
            } else {
                prev = previous_transforms[i];
                target = target_transforms[i];
            }
            
            Transform interpolated;
            interpolated.params1 = glm::mix(prev.params1, target.params1, interpolation_alpha);
            interpolated.params2 = glm::mix(prev.params2, target.params2, interpolation_alpha);
            interpolated.color = glm::mix(prev.color, target.color, interpolation_alpha);
            interpolated.variation = (interpolation_alpha < 0.5f) ? prev.variation : target.variation;
            
            interpolated_transforms.push_back(interpolated);
        }
        
        // Generate fractal every frame with the new interpolated data
        if (!interpolated_transforms.empty()) {
            generate_fractal_gpu(interpolated_transforms);
        }

        // --- Render Pass: Draw the generated points to the FBO ---
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(pointShaderProgram);
        glm::mat4 projection = glm::ortho(-2.0f, 2.0f, -2.0f, 2.0f, -1.0f, 1.0f);
        glUniformMatrix4fv(glGetUniformLocation(pointShaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
        
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        
        glBindVertexArray(vao);
        glDrawArrays(GL_POINTS, 0, TOTAL_POINTS);
        
        glDisable(GL_BLEND);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // --- Post-Processing Pass: Draw FBO texture to the screen quad ---
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        
        glUseProgram(quadShaderProgram);
        glUniform2f(glGetUniformLocation(quadShaderProgram, "u_resolution"), (float)SCR_WIDTH, (float)SCR_HEIGHT);
        glBindVertexArray(quad_vao);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, fbo_texture);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        glfwSwapBuffers(window);
    }

    // --- Cleanup ---
    glDeleteVertexArrays(1, &vao);
    glDeleteVertexArrays(1, &quad_vao);
    glDeleteBuffers(1, &quad_vbo);
    glDeleteBuffers(1, &transforms_ssbo);
    glDeleteBuffers(1, &points_ssbo);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &fbo_texture);
    glDeleteProgram(pointShaderProgram);
    glDeleteProgram(quadShaderProgram);
    glDeleteProgram(computeShaderProgram);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

void generate_fractal_gpu(const std::vector<Transform>& frame_transforms) {
    if (frame_transforms.empty()) return;
    
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, transforms_ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER, frame_transforms.size() * sizeof(Transform), frame_transforms.data(), GL_DYNAMIC_DRAW);
    
    glUseProgram(computeShaderProgram);
    glUniform1ui(glGetUniformLocation(computeShaderProgram, "num_transforms"), frame_transforms.size());
    glUniform1ui(glGetUniformLocation(computeShaderProgram, "total_points"), TOTAL_POINTS);
    unsigned int current_seed = (fractal_seed == 0) ? rd() : fractal_seed;
    glUniform1ui(glGetUniformLocation(computeShaderProgram, "seed"), current_seed);
    
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, transforms_ssbo);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, points_ssbo);
    
    GLuint num_groups = (TOTAL_POINTS + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
    glDispatchCompute(num_groups, 1, 1);
    
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void setup_gpu_compute() {
    glGenBuffers(1, &transforms_ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, transforms_ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER, 100 * sizeof(Transform), nullptr, GL_DYNAMIC_DRAW);

    glGenBuffers(1, &points_ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, points_ssbo);
    // This now correctly uses the TOTAL_POINTS value loaded from the config file in init_window()
    glBufferData(GL_SHADER_STORAGE_BUFFER, TOTAL_POINTS * sizeof(Point), nullptr, GL_STATIC_DRAW);
}

bool load_config_states(const std::string& filename, std::vector<std::vector<Transform>>& out_states) {
    out_states.clear();
    simpleini::INIReader reader;
    if (!reader.load(filename)) { 
        std::cerr << "Failed to load " << filename << std::endl;
        return false; 
    }
    
    const auto& config_data = reader.get_data();
    std::map<int, std::vector<Transform>> state_map;

    try {
        if (config_data.count("Settings")) {
            const auto& settings = config_data.at("Settings");
            if (settings.count("Width")) SCR_WIDTH = std::stoi(settings.at("Width"));
            if (settings.count("Height")) SCR_HEIGHT = std::stoi(settings.at("Height"));
            if (settings.count("InterpolationDuration")) INTERPOLATION_DURATION = std::stof(settings.at("InterpolationDuration"));
            if (settings.count("TotalPoints")) TOTAL_POINTS = std::stoll(settings.at("TotalPoints"));
            if (settings.count("Seed")) {
                // Use stoul for string to unsigned long, which fits unsigned int
                fractal_seed = std::stoul(settings.at("Seed"));
            }
            if (settings.count("AnimationMode")) {
                std::string mode_str = settings.at("AnimationMode");
                // Convert to lower case for case-insensitive comparison
                std::transform(mode_str.begin(), mode_str.end(), mode_str.begin(), ::tolower);
                if (mode_str == "loop") {
                    animation_mode = LOOP;
                } else if (mode_str == "random") {
                    animation_mode = RANDOM;
                } else {
                    animation_mode = PING_PONG;
                }
            }
        }
        
        for (const auto& pair : config_data) {
            const std::string& section_name = pair.first;
            if (section_name.rfind("State.", 0) == 0) { // Section name starts with "State."
                std::string temp = section_name.substr(6); // Remove "State."
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
            out_states.resize(max_state);
            for (const auto& pair : state_map) {
                if(pair.first > 0) out_states[pair.first - 1] = pair.second;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error parsing config file: " << e.what() << std::endl;
        return false;
    }
    return true;
}

GLFWwindow* init_window() {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return nullptr;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    
    // Load config once to get initial window size and point count
    std::vector<std::vector<Transform>> initial_states;
    load_config_states(CONFIG_FILENAME, initial_states);
    
    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "GPU Fractal Flame", NULL, NULL);
    if (!window) { 
        std::cerr << "Failed to create GLFW window" << std::endl; 
        glfwTerminate(); 
        return nullptr; 
    }
    
    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        return nullptr;
    }
    
    glEnable(GL_PROGRAM_POINT_SIZE);
    return window;
}

void create_framebuffer() {
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    glGenTextures(1, &fbo_texture);
    glBindTexture(GL_TEXTURE_2D, fbo_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, SCR_WIDTH, SCR_HEIGHT, 0, GL_RGB, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbo_texture, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "ERROR::FRAMEBUFFER:: Framebuffer is not complete!" << std::endl;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void create_screen_quad() {
    float quadVertices[] = { 
        // positions   // texCoords
        -1.0f,  1.0f,  0.0f, 1.0f,
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,

        -1.0f,  1.0f,  0.0f, 1.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f
    };

    glGenVertexArrays(1, &quad_vao); 
    glGenBuffers(1, &quad_vbo);
    glBindVertexArray(quad_vao); 
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
    
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
}
