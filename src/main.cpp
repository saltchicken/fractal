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
    glm::vec4 params1; // x=a, y=b, z=c, w=d
    glm::vec4 params2; // x=e, y=f
    glm::vec4 color;
    glm::uvec4 variation; // .x = variation_enum
};

// --- Global State ---
std::vector<Transform> transforms;
GLuint fbo, fbo_texture, quad_vao, quad_vbo;
GLuint computeShaderProgram, pointShaderProgram;
GLuint transforms_ssbo, points_ssbo;
bool should_regenerate = true;
std::filesystem::file_time_type last_config_time;
std::random_device rd;
GLuint timer_query; // For GPU timing

// --- Function Prototypes ---
void check_config_changes();
void generate_fractal_gpu();
GLFWwindow* init_window();
void create_framebuffer();
void create_screen_quad();
void setup_gpu_compute();
bool load_config(const std::string& filename);

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
    
    // A VAO is still needed for rendering, but it doesn't need any vertex buffers
    GLuint vao;
    glGenVertexArrays(1, &vao);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        check_config_changes();
        
        if (should_regenerate) {
            load_config(CONFIG_FILENAME);
            generate_fractal_gpu();
            should_regenerate = false;
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
        glDrawArrays(GL_POINTS, 0, TOTAL_POINTS); // Draw directly from SSBO
        
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

void generate_fractal_gpu() {
    if (transforms.empty()) return;

    std::cout << "Dispatching GPU to generate " << TOTAL_POINTS << " points..." << std::flush;
    
    auto start_time_cpu = std::chrono::high_resolution_clock::now();
    glBeginQuery(GL_TIME_ELAPSED, timer_query);

    // Update the transforms buffer on the GPU
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, transforms_ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER, transforms.size() * sizeof(Transform), transforms.data(), GL_DYNAMIC_DRAW);
    
    // Run the compute shader
    glUseProgram(computeShaderProgram);
    glUniform1ui(glGetUniformLocation(computeShaderProgram, "num_transforms"), transforms.size());
    glUniform1ui(glGetUniformLocation(computeShaderProgram, "total_points"), TOTAL_POINTS);
    glUniform1ui(glGetUniformLocation(computeShaderProgram, "seed"), rd()); 
    
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, transforms_ssbo);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, points_ssbo);
    
    // Dispatch the work
    GLuint num_groups = (TOTAL_POINTS + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
    glDispatchCompute(num_groups, 1, 1);
    
    // This barrier ensures the commands are processed before the timer ends
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    
    glEndQuery(GL_TIME_ELAPSED);

    // Wait for the GPU result and retrieve it
    GLint done = 0;
    while (!done) {
        glGetQueryObjectiv(timer_query, GL_QUERY_RESULT_AVAILABLE, &done);
    }
    GLuint64 elapsed_gpu_ns;
    glGetQueryObjectui64v(timer_query, GL_QUERY_RESULT, &elapsed_gpu_ns);
    
    auto end_time_cpu = std::chrono::high_resolution_clock::now();

    // --- Report Results ---
    double elapsed_gpu_ms = elapsed_gpu_ns / 1000000.0;
    std::chrono::duration<double, std::milli> elapsed_cpu_ms = end_time_cpu - start_time_cpu;

    std::cout << " Done." << std::endl;
    std::cout << "  - GPU Execution Time:   " << elapsed_gpu_ms << " ms" << std::endl;
    std::cout << "  - CPU Wait Time (Total): " << elapsed_cpu_ms.count() << " ms" << std::endl;
}

void setup_gpu_compute() {
    // Create Shader Storage Buffer Objects (SSBOs)
    glGenBuffers(1, &transforms_ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, transforms_ssbo);
    // Allocate a reasonable max size for transforms
    glBufferData(GL_SHADER_STORAGE_BUFFER, 100 * sizeof(Transform), nullptr, GL_DYNAMIC_DRAW);

    glGenBuffers(1, &points_ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, points_ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER, TOTAL_POINTS * sizeof(Point), nullptr, GL_STATIC_DRAW);
}

bool load_config(const std::string& filename) {
    transforms.clear();
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
                t.color = glm::vec4(color_vec, 1.0f);
            }
            if (section.count("variation")) {
                std::string var_str = section.at("variation");
                if (variation_map.count(var_str)) { t.variation.x = variation_map.at(var_str); }
            }
            transforms.push_back(t);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error parsing config file: " << e.what() << std::endl;
        return false;
    }

    try {
        last_config_time = std::filesystem::last_write_time(filename);
    } catch(const std::filesystem::filesystem_error& e) {
        std::cerr << "Warning: Could not get timestamp for " << filename << std::endl;
    }
    return true;
}

void check_config_changes() {
    try {
        auto current_config_time = std::filesystem::last_write_time(CONFIG_FILENAME);
        if (current_config_time > last_config_time) {
            std::cout << "Detected change in " << CONFIG_FILENAME << ". Flagging for regeneration." << std::endl;
            should_regenerate = true;
            last_config_time = current_config_time;
        }
    } catch (const std::filesystem::filesystem_error& e) {}
}

GLFWwindow* init_window() {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return nullptr;
    }
    // Compute shaders require OpenGL 4.3+
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    
    load_config(CONFIG_FILENAME); // Load once to get window size

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
