// main.cpp
// An improved real-time fractal flame renderer using an FBO for accumulation.

#include <iostream>
#include <vector>
#include <random>
#include <cmath>
#include <string>

// OpenGL / Windowing
#include <glad/glad.h>
#include <GLFW/glfw3.h>

// UI
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

// Math
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// --- Configuration ---
const unsigned int SCR_WIDTH = 1280;
const unsigned int SCR_HEIGHT = 720;
const int POINTS_PER_FRAME = 15000;

// --- Data Structures ---
struct Point {
    glm::vec2 position;
    glm::vec4 color;
};

enum Variation {
    LINEAR, SINUSOIDAL, SPHERICAL, SWIRL, HORSESHOE
};

const char* variation_names[] = { "Linear", "Sinusoidal", "Spherical", "Swirl", "Horseshoe" };

struct Transform {
    float a = 1.0f, b = 0.0f, c = 0.0f;
    float d = 0.0f, e = 1.0f, f = 0.0f;
    glm::vec3 color = glm::vec3(1.0f, 1.0f, 1.0f);
    Variation variation = LINEAR;
    int id;
    Transform() : id(next_id++) {}
private:
    static int next_id;
};
int Transform::next_id = 0;

// --- Point Shader Code ---
const char* pointVertexShaderSource = R"(
    #version 330 core
    layout (location = 0) in vec2 aPos;
    layout (location = 1) in vec4 aColor;
    out vec4 fragColor;
    uniform mat4 projection;
    void main() {
        gl_Position = projection * vec4(aPos.x, aPos.y, 0.0, 1.0);
        fragColor = aColor;
    }
)";

const char* pointFragmentShaderSource = R"(
    #version 330 core
    in vec4 fragColor;
    out vec4 FragColor;
    void main() {
        FragColor = fragColor;
    }
)";

// Shaders for drawing the final texture to a screen-sized quad
const char* quadVertexShaderSource = R"(
    #version 330 core
    layout (location = 0) in vec2 aPos;
    layout (location = 1) in vec2 aTexCoords;
    out vec2 TexCoords;
    void main() {
        TexCoords = aTexCoords;
        gl_Position = vec4(aPos.x, aPos.y, 0.0, 1.0);
    }
)";

const char* quadFragmentShaderSource = R"(
    #version 330 core
    out vec4 FragColor;
    in vec2 TexCoords;
    uniform sampler2D screenTexture;
    void main() {
        vec3 color = texture(screenTexture, TexCoords).rgb;
        color = pow(color, vec3(0.8)); // Gamma correction
        FragColor = vec4(color, 1.0);
    }
)";


// --- Global State ---
std::vector<Transform> transforms;
std::vector<Point> points;
glm::vec2 current_point(0.0f, 0.0f);
bool params_changed = true;

GLuint fbo;
GLuint fbo_texture;
GLuint quad_vao, quad_vbo;

// --- Function Prototypes ---
GLFWwindow* init_window();
unsigned int create_shader_program(const char* vs_source, const char* fs_source);
void create_framebuffer();
void create_screen_quad();
void apply_variations(glm::vec2& p, Variation var);
void generate_points();
void render_ui();


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

    transforms.emplace_back();
    transforms[0].color = glm::vec3(0.0, 0.0, 1.0);
    transforms[0].variation = SINUSOIDAL;
    transforms.emplace_back();
    transforms[1].a = 0.0f; transforms[1].b = 0.5f; transforms[1].c = 0.0f;
    transforms[1].d = -0.5f; transforms[1].e = 0.0f; transforms[1].f = 0.0f;
    transforms[1].color = glm::vec3(1.0, 0.0, 0.0);
    transforms[1].variation = SPHERICAL;

    // --- Main Render Loop ---
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        if (params_changed) {
            current_point = glm::vec2(0.0f, 0.0f);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            params_changed = false;
        }

        generate_points();
        
        // 1. Draw points into the FBO
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glUseProgram(pointShaderProgram);
        
        glm::mat4 projection = glm::ortho(-2.0f, 2.0f, -2.0f, 2.0f, -1.0f, 1.0f);
        glUniformMatrix4fv(glGetUniformLocation(pointShaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, points.size() * sizeof(Point), points.data());
        
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);

        glBindVertexArray(vao);
        glDrawArrays(GL_POINTS, 0, points.size());
        
        glDisable(GL_BLEND);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // 2. Draw the FBO texture to the screen
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(quadShaderProgram);
        glBindVertexArray(quad_vao);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, fbo_texture);
        glUniform1i(glGetUniformLocation(quadShaderProgram, "screenTexture"), 0);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        
        // 3. Render the UI on top
        render_ui();

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

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}


// --- Function Implementations ---

void generate_points() {
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
        if (i > 20) {
            points.push_back({p, glm::vec4(t.color, 0.15f)}); 
        }
    }
}

void apply_variations(glm::vec2& p, Variation var) {
    switch (var) {
        case LINEAR: break;
        case SINUSOIDAL: p = glm::sin(p); break;
        case SPHERICAL: {
            float r2 = glm::dot(p, p);
            if (r2 > 1e-6) p /= r2;
            break;
        }
        case SWIRL: {
            float r2 = glm::dot(p, p);
            float s = sin(r2);
            float c = cos(r2);
            p = glm::vec2(p.x * s - p.y * c, p.x * c + p.y * s);
            break;
        }
        case HORSESHOE: {
            float r = glm::length(p);
            if (r > 1e-6) {
                float x_minus_y = p.x - p.y;
                float x_plus_y = p.x + p.y;
                p.x = (1.0f / r) * x_minus_y * x_plus_y;
                p.y = (1.0f / r) * 2.0f * p.x * p.y;
            }
            break;
        }
    }
}

void render_ui() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGui::Begin("Flame Controls");

    if (ImGui::Button("Add New Transform")) {
        transforms.emplace_back();
        params_changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset Simulation")) {
        params_changed = true;
    }
    ImGui::Separator();
    
    // Using a C-style loop to safely remove elements while iterating
    for (size_t i = 0; i < transforms.size(); ++i) {
        Transform& t = transforms[i];
        ImGui::PushID(t.id);

        std::string header_name = "Transform " + std::to_string(i + 1);
        if (ImGui::CollapsingHeader(header_name.c_str())) {
            
            if (ImGui::Combo("Variation", (int*)&t.variation, variation_names, IM_ARRAYSIZE(variation_names))) {
                 params_changed = true;
            }
            if (ImGui::ColorEdit3("Color", glm::value_ptr(t.color))) {
                params_changed = true;
            }
            if (ImGui::DragFloat("a", &t.a, 0.01f) || ImGui::DragFloat("b", &t.b, 0.01f) || ImGui::DragFloat("c", &t.c, 0.01f) ||
                ImGui::DragFloat("d", &t.d, 0.01f) || ImGui::DragFloat("e", &t.e, 0.01f) || ImGui::DragFloat("f", &t.f, 0.01f)) {
                params_changed = true;
            }
            if (ImGui::Button("Remove")) {
                transforms.erase(transforms.begin() + i);
                params_changed = true;
                i--; // Decrement loop counter to avoid skipping the next element
            }
        }
        ImGui::PopID();
    }
    ImGui::End();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

GLFWwindow* init_window() {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    #ifdef __APPLE__
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    #endif
    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Fractal Flame", NULL, NULL);
    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
    glEnable(GL_PROGRAM_POINT_SIZE);
    return window;
}

unsigned int create_shader_program(const char* vs_source, const char* fs_source) {
    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vs_source, NULL);
    glCompileShader(vertexShader);
    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fs_source, NULL);
    glCompileShader(fragmentShader);
    unsigned int shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    return shaderProgram;
}

void create_framebuffer() {
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenTextures(1, &fbo_texture);
    glBindTexture(GL_TEXTURE_2D, fbo_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, SCR_WIDTH, SCR_HEIGHT, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
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
