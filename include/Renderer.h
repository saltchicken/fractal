#pragma once
#include "Config.h"
#include <glad/glad.h>
#include <vector>
#include <random>

struct Point {
    glm::vec4 position;
    glm::vec4 color;
};

class Renderer {
public:
    Renderer();
    ~Renderer();

    bool init(const Config& initial_config);

    void render(const Config& config, const std::vector<Transform>& transforms, unsigned int width, unsigned int height);

    void onWindowResize(int width, int height);

    void resetGPUResources(const Config& config);

private:
    void cleanup();
    void createShaders();
    void createFramebuffers();
    void createScreenQuad();
    void createGPUComputeBuffers(const Config& config);
    void queryUniformLocations();
    void generateFractalOnGPU(const Config& config, const std::vector<Transform>& transforms);

    // OpenGL Handles
    GLuint m_fbo = 0, m_fbo_texture = 0, m_quad_vao = 0, m_quad_vbo = 0;
    GLuint m_accumulation_fbo = 0, m_accumulation_texture = 0;
    GLuint m_compute_shader_program = 0, m_point_shader_program = 0, m_quad_shader_program = 0;
    GLuint m_fade_shader_program = 0;
    GLuint m_transforms_ssbo = 0, m_points_ssbo = 0;
    GLuint m_point_render_vao = 0;

    // Uniform Locations
    GLint m_proj_loc, m_res_loc, m_brightness_loc, m_contrast_loc, m_gamma_loc;
    GLint m_accumulation_sampler_loc, m_blend_factor_loc, m_persistence_loc;
    GLint m_num_transforms_loc, m_total_points_loc, m_seed_loc;

    // Internal state
    unsigned int m_width = 0;
    unsigned int m_height = 0;
    std::random_device m_rd; // For generating compute shader seeds
};
