#pragma once

#include "Config.h"
#include <glad/glad.h>
#include <memory>
#include <vector>
#include <random>

struct Point {
    glm::vec4 position;
    glm::vec4 color;
};

class Shader; // Forward declaration

class Renderer {
public:
    Renderer();
    ~Renderer();

    bool init(const Config& initial_config);
    void render(const Config& config, const std::vector<Transform>& transforms, unsigned int width, unsigned int height, bool is_transparent);
    void onWindowResize(int width, int height);
    void resetGPUResources(const Config& config);

private:
    void cleanup();
    void createShaders();
    void createFramebuffers();
    void createScreenQuad();
    void createGPUComputeBuffers(const Config& config);
    void generateFractalOnGPU(const Config& config, const std::vector<Transform>& transforms);

    // OpenGL Handles
    GLuint m_fbo = 0, m_fbo_texture = 0, m_quad_vao = 0, m_quad_vbo = 0;
    GLuint m_accumulation_fbo = 0, m_accumulation_texture = 0;
    GLuint m_transforms_ssbo = 0, m_points_ssbo = 0;
    GLuint m_point_render_vao = 0;

    // RAII Shader Objects
    std::unique_ptr<Shader> m_computeShader;
    std::unique_ptr<Shader> m_pointShader;
    std::unique_ptr<Shader> m_quadShader;
    std::unique_ptr<Shader> m_fadeShader;

    // Internal state
    unsigned int m_width = 0;
    unsigned int m_height = 0;
    std::random_device m_rd; // For generating compute shader seeds
};

