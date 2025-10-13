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

// --- Global State ---
// NEW: State management for interpolation
std::vector<Transform> previous_transforms;
std::vector<Transform> target_transforms;
float interpolation_alpha = 1.0f; // 0.0 = previous, 1.0 = target
const float INTERPOLATION_DURATION = 0.75f; // seconds

std::filesystem::file_time_type last_config_time;
std::random_device rd;
GLuint fbo, fbo_texture, quad_vao, quad_vbo;
GLuint computeShaderProgram, pointShaderProgram;
GLuint transforms_ssbo, points_ssbo;
GLuint timer_query;
double last_frame_time = 0.0;

// --- Function Prototypes ---
bool check_and_handle_config_changes();
void generate_fractal_gpu(const std::vector<Transform>& frame_transforms); // MODIFIED
GLFWwindow* init_window();
void create_framebuffer();
void create_screen_quad();
void setup_gpu_compute();
bool load_config(const std::string& filename, std::vector<Transform>& out_transforms); // MODIFIED

int main() {
    GLFWwindow* window = init_window();
    if (!window) return -1;
    
    pointShaderProgram = create_shader_program_from_files("shaders/point.vert", "shaders/point.frag");
    unsigned int quadShaderProgram = create_shader_program_from_files("shaders/quad.vert", "shaders/quad.frag");
    computeShaderProgram = create_compute_shader_program_from_file("shaders/fractal.comp");

    create_framebuffer();
    create_screen_quad();
    setup_gpu_compute();
    
    glGenQueries(1, &timer_query);
    
    // Initial config load
    if (!load_config(CONFIG_FILENAME, target_transforms)) {
        std::cerr << "Initial config load failed. Please ensure " << CONFIG_FILENAME << " exists." << std::endl;
    }
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
        check_and_handle_config_changes();
        
        // --- Interpolation Logic (runs every frame) ---
        if (interpolation_alpha < 1.0f) {
            interpolation_alpha += delta_time / INTERPOLATION_DURATION;
            interpolation_alpha = std::min(1.0f, interpolation_alpha);
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
                // Fading out: The target is itself but with zero alpha.
                prev = previous_transforms[i];
                target = previous_transforms[i]; 
                target.color.a = 0.0f;
            } else if (is_appearing) {
                // Fading in: The previous state is the target but with zero alpha.
                target = target_transforms[i];
                prev = target_transforms[i]; 
                prev.color.a = 0.0f;
            } else {
                // Standard interpolation.
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
    glDeleteQueries(1, &timer_query);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

// MODIFIED: Accepts transforms for the current frame
void generate_fractal_gpu(const std::vector<Transform>& frame_transforms) {
    if (frame_transforms.empty()) return;
    // The console output can be noisy, so it's commented out for continuous generation.
    // std::cout << "Dispatching GPU to generate " << TOTAL_POINTS << " points..." << std::flush;
    
    // glBeginQuery(GL_TIME_ELAPSED, timer_query);

    // Update the transforms buffer on the GPU
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, transforms_ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER, frame_transforms.size() * sizeof(Transform), frame_transforms.data(), GL_DYNAMIC_DRAW);
    
    // Run the compute shader
    glUseProgram(computeShaderProgram);
    glUniform1ui(glGetUniformLocation(computeShaderProgram, "num_transforms"), frame_transforms.size());
    glUniform1ui(glGetUniformLocation(computeShaderProgram, "total_points"), TOTAL_POINTS);
    glUniform1ui(glGetUniformLocation(computeShaderProgram, "seed"), rd()); 
    
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, transforms_ssbo);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, points_ssbo);
    
    // Dispatch the work
    GLuint num_groups = (TOTAL_POINTS + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
    glDispatchCompute(num_groups, 1, 1);
    
    // Ensure compute shader finishes before we render the points
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    
    // Timing queries can add overhead and stalls; not ideal for the render loop.
    // glEndQuery(GL_TIME_ELAPSED);
    // ... timing query result retrieval would go here ...
}

void setup_gpu_compute() {
    glGenBuffers(1, &transforms_ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, transforms_ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER, 100 * sizeof(Transform), nullptr, GL_DYNAMIC_DRAW);

    glGenBuffers(1, &points_ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, points_ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER, TOTAL_POINTS * sizeof(Point), nullptr, GL_STATIC_DRAW);
}

// MODIFIED: Now populates a specific vector passed to it
bool load_config(const std::string& filename, std::vector<Transform>& out_transforms) {
    out_transforms.clear();
    simpleini::INIReader reader;
    if (!reader.load(filename)) { 
        std::cerr << "Failed to load " << filename << std::endl;
        return false; 
    }
    
    const auto& config_data = reader.get_data();
    try {
        if (config_data.count("Settings")) {
            const auto& settings = config_data.at("Settings");
            if (settings.count("Width")) SCR_WIDTH = std::stoi(settings.at("Width"));
            if (settings.count("Height")) SCR_HEIGHT = std::stoi(settings.at("Height"));
            
            long long new_total_points = TOTAL_POINTS;
            if (settings.count("TotalPoints")) new_total_points = std::stoll(settings.at("TotalPoints"));
            
            if (new_total_points != TOTAL_POINTS) {
                TOTAL_POINTS = new_total_points;
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, points_ssbo);
                glBufferData(GL_SHADER_STORAGE_BUFFER, TOTAL_POINTS * sizeof(Point), nullptr, GL_STATIC_DRAW);
                std::cout << "Total points changed to " << TOTAL_POINTS << ". Resized GPU buffer." << std::endl;
            }
        }
        
        int i = 1;
        while(true) {
            std::string section_name = "Transform." + std::to_string(i++);
            if (!config_data.count(section_name)) break;
            const auto& section = config_data.at(section_name);
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
                // NEW: Use a default alpha that matches the old hardcoded value
                t.color = glm::vec4(color_vec, 0.15f);
            }
            if (section.count("variation")) {
                std::string var_str = section.at("variation");
                if (variation_map.count(var_str)) { t.variation.x = variation_map.at(var_str); }
            }
            out_transforms.push_back(t);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error parsing config file: " << e.what() << std::endl;
        return false;
    }
    return true;
}

// MODIFIED: Renamed and now triggers the start of an interpolation
bool check_and_handle_config_changes() {
    try {
        auto current_config_time = std::filesystem::last_write_time(CONFIG_FILENAME);
        if (current_config_time > last_config_time) {
            std::cout << "Detected change in " << CONFIG_FILENAME << ". Starting interpolation." << std::endl;
            
            // Save the current state as the "previous" state
            previous_transforms = target_transforms;

            // Load the new config into the "target" state
            if (load_config(CONFIG_FILENAME, target_transforms)) {
                 // Reset the interpolation timer
                 interpolation_alpha = 0.0f;
                 last_config_time = current_config_time;
                 return true;
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {}
    return false;
}

GLFWwindow* init_window() {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return nullptr;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    
    // Load config once to get initial window size
    std::vector<Transform> initial_transforms;
    load_config(CONFIG_FILENAME, initial_transforms);
    
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

    try {
        last_config_time = std::filesystem::last_write_time(CONFIG_FILENAME);
    } catch(const std::filesystem::filesystem_error& e) {
        std::cerr << "Warning: Could not get initial timestamp for " << CONFIG_FILENAME << std::endl;
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
