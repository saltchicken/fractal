// src/main.cpp

// A single-pass, auto-reloading fractal flame renderer.
#include <iostream>
#include <vector>
#include <random>
#include <cmath>
#include <string>
#include <map>
#include <sstream>
#include <filesystem>
#include <chrono>

// Custom INI Parser
#include "ini.h"

// OpenGL / Windowing
#include <glad/glad.h>
#include <GLFW/glfw3.h>

// Math
#include <glm.hpp>
#include <gtc/matrix_transform.hpp>
#include <gtc/type_ptr.hpp>

// --- Configuration ---
const std::string CONFIG_FILENAME = "config.ini";
unsigned int SCR_WIDTH = 1280;
unsigned int SCR_HEIGHT = 720;
int POINTS_PER_FRAME = 50000;
long long TOTAL_POINTS = 5000000;

// --- Data Structures (unchanged) ---
struct Point { glm::vec2 position; glm::vec4 color; };
enum Variation { LINEAR, SINUSOIDAL, SPHERICAL, SWIRL, HORSESHOE };
const std::map<std::string, Variation> variation_map = {
    {"LINEAR", LINEAR}, {"SINUSOIDAL", SINUSOIDAL}, {"SPHERICAL", SPHERICAL},
    {"SWIRL", SWIRL}, {"HORSESHOE", HORSESHOE}
};
struct Transform {
    float a = 1.0f, b = 0.0f, c = 0.0f; float d = 0.0f, e = 1.0f, f = 0.0f;
    glm::vec3 color = glm::vec3(1.0f, 1.0f, 1.0f); Variation variation = LINEAR;
};

// --- Shader Code (unchanged) ---
const char* pointVertexShaderSource = R"(#version 330 core
layout (location = 0) in vec2 aPos; layout (location = 1) in vec4 aColor;
out vec4 fragColor; uniform mat4 projection;
void main() { gl_Position = projection * vec4(aPos, 0.0, 1.0); fragColor = aColor; })";
const char* pointFragmentShaderSource = R"(#version 330 core
in vec4 fragColor; out vec4 FragColor; void main() { FragColor = fragColor; })";
const char* quadVertexShaderSource = R"(#version 330 core
layout (location = 0) in vec2 aPos; layout (location = 1) in vec2 aTexCoords;
out vec2 TexCoords; void main() { TexCoords = aTexCoords; gl_Position = vec4(aPos, 0.0, 1.0); })";
const char* quadFragmentShaderSource = R"(#version 330 core
out vec4 FragColor; in vec2 TexCoords; uniform sampler2D screenTexture;
void main() { vec3 color = texture(screenTexture, TexCoords).rgb;
color = pow(color, vec3(0.8)); FragColor = vec4(color, 1.0); })";

// --- Global State ---
std::vector<Transform> transforms;
std::vector<Point> points;
glm::vec2 current_point(0.0f, 0.0f);
GLuint fbo;
GLuint fbo_texture;
GLuint quad_vao, quad_vbo;
bool should_regenerate = true;
std::filesystem::file_time_type last_config_time;

// --- Function Prototypes ---
void check_config_changes();
void generate_fractal(GLuint pointShaderProgram, GLuint vao, GLuint vbo);
GLFWwindow* init_window();
unsigned int create_shader_program(const char* vs_source, const char* fs_source);
void create_framebuffer();
void create_screen_quad();
void apply_variations(glm::vec2& p, Variation var);
void generate_points_batch(long long points_already_generated); // <--- CHANGE 1: Added parameter
bool load_config(const std::string& filename);

// --- Main Function ---
int main() {
    GLFWwindow* window = init_window();
    if (!window) return -1;

    unsigned int pointShaderProgram = create_shader_program(pointVertexShaderSource, pointFragmentShaderSource);
    unsigned int quadShaderProgram = create_shader_program(quadVertexShaderSource, quadFragmentShaderSource);
    
    create_framebuffer();
    create_screen_quad();

    GLuint vao, vbo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, POINTS_PER_FRAME * sizeof(Point), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Point), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Point), (void*)offsetof(Point, color));
    glEnableVertexAttribArray(1);
    
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        check_config_changes();

        if (should_regenerate) {
            load_config(CONFIG_FILENAME);
            generate_fractal(pointShaderProgram, vao, vbo);
            should_regenerate = false;
        }

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(quadShaderProgram);
        glBindVertexArray(quad_vao);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, fbo_texture);
        glUniform1i(glGetUniformLocation(quadShaderProgram, "screenTexture"), 0);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glfwSwapBuffers(window);
    }

    // --- Cleanup ---
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &quad_vao);
    glDeleteBuffers(1, &quad_vbo);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &fbo_texture);
    glDeleteProgram(pointShaderProgram);
    glDeleteProgram(quadShaderProgram);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

// --- Function Implementations ---
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

void generate_fractal(GLuint pointShaderProgram, GLuint vao, GLuint vbo) {
    std::cout << "Generating fractal with " << TOTAL_POINTS << " points..." << std::endl;
    current_point = glm::vec2(0.0f, 0.0f);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(pointShaderProgram);
    glm::mat4 projection = glm::ortho(-2.0f, 2.0f, -2.0f, 2.0f, -1.0f, 1.0f);
    glUniformMatrix4fv(glGetUniformLocation(pointShaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    
    long long points_generated = 0;
    while(points_generated < TOTAL_POINTS) {
        generate_points_batch(points_generated); // <--- CHANGE 2: Pass the variable
        
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, points.size() * sizeof(Point), points.data());
        glBindVertexArray(vao);
        glDrawArrays(GL_POINTS, 0, points.size());
        
        points_generated += points.size();
        int progress = static_cast<int>((static_cast<double>(points_generated) / TOTAL_POINTS) * 100);
        std::cout << "\rProgress: " << progress << "%" << std::flush;
    }
    std::cout << "\nGeneration complete." << std::endl;

    glDisable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
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
            if (settings.count("PointsPerFrame")) POINTS_PER_FRAME = std::stoi(settings.at("PointsPerFrame"));
            if (settings.count("TotalPoints")) TOTAL_POINTS = std::stoll(settings.at("TotalPoints"));
        }
        int i = 1;
        while (true) {
            std::string section_name = "Transform." + std::to_string(i);
            if (!config_data.count(section_name)) break;
            const auto& section = config_data.at(section_name);
            Transform t;
            if (section.count("a")) t.a = std::stof(section.at("a"));
            if (section.count("b")) t.b = std::stof(section.at("b"));
            if (section.count("c")) t.c = std::stof(section.at("c"));
            if (section.count("d")) t.d = std::stof(section.at("d"));
            if (section.count("e")) t.e = std::stof(section.at("e"));
            if (section.count("f")) t.f = std::stof(section.at("f"));
            if (section.count("color")) {
                std::stringstream ss(section.at("color"));
                ss >> t.color.r; ss.ignore(); ss >> t.color.g; ss.ignore(); ss >> t.color.b;
            }
            if (section.count("variation")) {
                std::string var_str = section.at("variation");
                if (variation_map.count(var_str)) { t.variation = variation_map.at(var_str); } 
                else { std::cerr << "Warning: Unknown variation '" << var_str << "' in " << section_name << std::endl; }
            }
            transforms.push_back(t);
            i++;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error parsing config file: " << e.what() << std::endl;
        return false;
    }
    if (transforms.empty()) { std::cerr << "Warning: No transforms loaded." << std::endl; }
    
    try {
        last_config_time = std::filesystem::last_write_time(filename);
    } catch(const std::filesystem::filesystem_error& e) {
        std::cerr << "Warning: Could not get timestamp for " << filename << ". Auto-reloading may not work." << std::endl;
    }
    return true;
}

void generate_points_batch(long long points_already_generated) { // <--- CHANGE 3: Accept the parameter
    static std::random_device rd;
    static std::mt19937 gen(rd());
    if (transforms.empty()) return;
    std::uniform_int_distribution<> dis(0, transforms.size() - 1);
    points.clear();
    points.reserve(POINTS_PER_FRAME);
    for (int i = 0; i < POINTS_PER_FRAME; ++i) {
        int transform_idx = dis(gen);
        const auto& t = transforms[transform_idx];
        float x_new = t.a * current_point.x + t.b * current_point.y + t.c;
        float y_new = t.d * current_point.x + t.e * current_point.y + t.f;
        glm::vec2 p(x_new, y_new);
        apply_variations(p, t.variation);
        current_point = p;
        if (points_already_generated + i > 20) { 
            points.push_back({p, glm::vec4(t.color, 0.15f)});
        }
    }
}

void apply_variations(glm::vec2& p, Variation var) {
    switch (var) {
        case LINEAR: break;
        case SINUSOIDAL: p = glm::sin(p); break;
        case SPHERICAL: { float r2 = glm::dot(p, p); if (r2 > 1e-6) p /= r2; break; }
        case SWIRL: { float r2 = glm::dot(p, p); float s = sin(r2); float c = cos(r2);
            p = glm::vec2(p.x * s - p.y * c, p.x * c + p.y * s); break; }
        case HORSESHOE: { float r = glm::length(p); if (r > 1e-6) {
                p.x = (1.0f / r) * (p.x - p.y) * (p.x + p.y);
                p.y = (1.0f / r) * 2.0f * p.x * p.y; } break; }
    }
}

GLFWwindow* init_window() {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return nullptr;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    load_config(CONFIG_FILENAME);

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Fractal Flame", NULL, NULL);
    if (!window) { std::cerr << "Failed to create GLFW window" << std::endl; glfwTerminate(); return nullptr; }
    
    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        return nullptr;
    }
    glEnable(GL_PROGRAM_POINT_SIZE);
    return window;
}

unsigned int create_shader_program(const char* vs_source, const char* fs_source) {
    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vs_source, NULL); glCompileShader(vertexShader);
    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fs_source, NULL); glCompileShader(fragmentShader);
    unsigned int shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader); glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);
    glDeleteShader(vertexShader); glDeleteShader(fragmentShader);
    return shaderProgram;
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
    float quadVertices[] = { -1.0f, 1.0f, 0.0f, 1.0f, -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, -1.0f, 1.0f, 0.0f,
        -1.0f, 1.0f, 0.0f, 1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    glGenVertexArrays(1, &quad_vao); glGenBuffers(1, &quad_vbo);
    glBindVertexArray(quad_vao); glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
}
