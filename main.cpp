// main.cpp
// A simple real-time fractal flame renderer using C++, OpenGL, and Dear ImGui.

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
const int POINTS_PER_FRAME = 75000;

// --- Data Structures ---

// A simple 2D point with a color
struct Point {
    glm::vec2 position;
    glm::vec4 color;
};

// The non-linear functions applied to the points
enum Variation {
    LINEAR,
    SINUSOIDAL,
    SPHERICAL,
    SWIRL,
    HORSESHOE
};

const char* variation_names[] = { "Linear", "Sinusoidal", "Spherical", "Swirl", "Horseshoe" };

// A single transform in our Iterated Function System (IFS)
struct Transform {
    // Affine transform coefficients
    float a = 1.0f, b = 0.0f, c = 0.0f;
    float d = 0.0f, e = 1.0f, f = 0.0f;

    // Color (r, g, b) and variation type
    glm::vec3 color = glm::vec3(1.0f, 1.0f, 1.0f);
    Variation variation = LINEAR;

    // Unique ID for ImGui
    int id;
    Transform() : id(next_id++) {}

private:
    static int next_id;
};
int Transform::next_id = 0;


// --- Shader Code (as C-style strings) ---
const char* vertexShaderSource = R"(
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

const char* fragmentShaderSource = R"(
    #version 330 core
    in vec4 fragColor;
    out vec4 FragColor;

    void main() {
        FragColor = fragColor;
    }
)";

// --- Global State ---
std::vector<Transform> transforms;
std::vector<Point> points;
glm::vec2 current_point(0.0f, 0.0f);
bool params_changed = true; // Flag to reset the simulation when UI changes

// --- Function Prototypes ---
GLFWwindow* init_window();
unsigned int setup_shaders();
void apply_variations(glm::vec2& p, Variation var);
void generate_points();
void render_ui();


// --- Main Function ---
int main() {
    // 1. Initialization
    GLFWwindow* window = init_window();
    if (!window) return -1;

    unsigned int shaderProgram = setup_shaders();

    // 2. Setup OpenGL Buffers
    GLuint vao, vbo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, POINTS_PER_FRAME * sizeof(Point), nullptr, GL_STREAM_DRAW);

    // Position attribute
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Point), (void*)0);
    glEnableVertexAttribArray(0);
    // Color attribute
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Point), (void*)offsetof(Point, color));
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    
    // 3. Setup Initial Fractal Flame ("Gnarl" style preset)
    transforms.emplace_back();
    transforms[0].a = 0.99f; transforms[0].b = 0.0f; transforms[0].c = 0.0f;
    transforms[0].d = 0.0f;  transforms[0].e = 0.99f; transforms[0].f = 0.0f;
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

        // If UI changed parameters, clear the old points
        if (params_changed) {
            points.clear();
            current_point = glm::vec2(0.0f, 0.0f);
            params_changed = false;
        }

        // 4. Generate a new batch of points on the CPU
        generate_points();

        // 5. Rendering
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(shaderProgram);

        // Setup projection matrix to map our points' space to screen space
        glm::mat4 projection = glm::ortho(-2.0f, 2.0f, -2.0f, 2.0f, -1.0f, 1.0f);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

        // Update VBO with the newly generated points
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, points.size() * sizeof(Point), points.data());
        
        // Enable additive blending for the density effect
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);

        // Draw the points
        glBindVertexArray(vao);
        glDrawArrays(GL_POINTS, 0, points.size());
        
        glDisable(GL_BLEND);

        // 6. Render the UI on top
        render_ui();

        glfwSwapBuffers(window);
    }

    // --- Cleanup ---
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    glDeleteProgram(shaderProgram);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}


// --- Function Implementations ---

void generate_points() {
    // A simple random number generator
    static std::random_device rd;
    static std::mt19937 gen(rd());

    if (transforms.empty()) return;

    // Create the distribution here so it has the correct range on every call
    std::uniform_int_distribution<> dis(0, transforms.size() - 1);

    points.clear();
    points.reserve(POINTS_PER_FRAME);

    for (int i = 0; i < POINTS_PER_FRAME; ++i) {
        // 1. Pick a random transform
        int transform_idx = dis(gen);
        const auto& t = transforms[transform_idx];

        // 2. Apply affine transform
        float x_new = t.a * current_point.x + t.b * current_point.y + t.c;
        float y_new = t.d * current_point.x + t.e * current_point.y + t.f;
        glm::vec2 p(x_new, y_new);

        // 3. Apply non-linear variation
        apply_variations(p, t.variation);

        // 4. Update the current point
        current_point = p;
        
        // Add the point to our render buffer (we skip the first 20 to let it settle)
        if (i > 20) {
            points.push_back({p, glm::vec4(t.color, 0.04f)}); // Low alpha for blending
        }
    }
}

void apply_variations(glm::vec2& p, Variation var) {
    switch (var) {
        case LINEAR:
            // Do nothing
            break;
        case SINUSOIDAL:
            p.x = sin(p.x);
            p.y = sin(p.y);
            break;
        case SPHERICAL: {
            float r2 = p.x * p.x + p.y * p.y;
            if (r2 > 1e-6) { // Avoid division by zero
                p.x /= r2;
                p.y /= r2;
            }
            break;
        }
        case SWIRL: {
            float r2 = p.x * p.x + p.y * p.y;
            float sin_r2 = sin(r2);
            float cos_r2 = cos(r2);
            float new_x = p.x * sin_r2 - p.y * cos_r2;
            float new_y = p.x * cos_r2 + p.y * sin_r2;
            p.x = new_x;
            p.y = new_y;
            break;
        }
        case HORSESHOE: {
            float r = sqrt(p.x * p.x + p.y * p.y);
            if (r > 1e-6) {
                p.x = (1.0f / r) * (p.x - p.y) * (p.x + p.y);
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
    
    // Use a C-style loop to safely remove elements
    for (size_t i = 0; i < transforms.size(); ++i) {
        Transform& t = transforms[i];
        ImGui::PushID(t.id); // Give each transform's controls a unique ID

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
                i--; // Decrement i to account for the removed element
            }
        }
        ImGui::PopID();
    }

    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

GLFWwindow* init_window() {
    // GLFW initialization
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return nullptr;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    
    #ifdef __APPLE__
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    #endif

    // Window creation
    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Fractal Flame", NULL, NULL);
    if (window == NULL) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return nullptr;
    }
    glfwMakeContextCurrent(window);

    // GLAD initialization
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        return nullptr;
    }
    
    // ImGui Initialization
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
    
    // Set OpenGL state
    glEnable(GL_PROGRAM_POINT_SIZE); // Allows shaders to control point size if needed
    
    return window;
}

unsigned int setup_shaders() {
    // Vertex shader
    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);
    // Fragment shader
    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);
    // Link shaders
    unsigned int shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);
    // Delete shaders as they're linked into our program now and no longer necessary
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    return shaderProgram;
}
