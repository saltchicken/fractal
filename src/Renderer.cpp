#include "Renderer.h"
#include "Shader.h" // Now a class
#include <iostream>
#include <gtc/matrix_transform.hpp>
#include <gtc/type_ptr.hpp>

Renderer::Renderer() {
    // Constructor can be empty if all initialization is in init()
}

Renderer::~Renderer() {
    cleanup();
}

bool Renderer::init(const Config& initial_config) {
    m_width = initial_config.getWidth();
    m_height = initial_config.getHeight();

    createShaders();
    createFramebuffers();
    createScreenQuad();
    createGPUComputeBuffers(initial_config);

    // This VAO is just a handle that tells OpenGL how to interpret the SSBO data
    // when we make the point drawing call. No actual vertex data is uploaded here.
    glGenVertexArrays(1, &m_point_render_vao);
    
    return true;
}

void Renderer::cleanup() {
    // The unique_ptr members for shaders are cleaned up automatically.
    // No glDeleteProgram calls are needed here.
    glDeleteVertexArrays(1, &m_point_render_vao);
    glDeleteVertexArrays(1, &m_quad_vao);
    glDeleteBuffers(1, &m_quad_vbo);
    glDeleteBuffers(1, &m_transforms_ssbo);
    glDeleteBuffers(1, &m_points_ssbo);
    glDeleteFramebuffers(1, &m_fbo);
    glDeleteTextures(1, &m_fbo_texture);
    glDeleteFramebuffers(1, &m_accumulation_fbo);
    glDeleteTextures(1, &m_accumulation_texture);
}

void Renderer::onWindowResize(int width, int height) {
    if (width == 0 || height == 0) return; // Avoid issues when minimized
    m_width = static_cast<unsigned int>(width);
    m_height = static_cast<unsigned int>(height);
    createFramebuffers(); // Recreate FBOs and textures with the new dimensions
    std::cout << "Renderer resized to " << m_width << "x" << m_height << std::endl;
}

void Renderer::resetGPUResources(const Config& config) {
    createGPUComputeBuffers(config);
    // It's important to clear the accumulation buffer after a reset
    // to avoid visual artifacts from the previous state.
    glBindFramebuffer(GL_FRAMEBUFFER, m_accumulation_fbo);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::render(const Config& config, const std::vector<Transform>& transforms, unsigned int width, unsigned int height, bool is_transparent, bool is_paused) {
    if (!is_paused) {
        // --- PART 0: GPU Compute ---
        if (!transforms.empty()) {
            generateFractalOnGPU(config, transforms);
        }

        // --- PART 1: Render Raw Points with Motion Blur ---
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glEnable(GL_BLEND);
        // A. Fade Pass
        m_fadeShader->use();
        m_fadeShader->setFloat("u_persistence", config.getPersistence());
        glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_ALPHA); // This blend mode darkens the texture
        glBindVertexArray(m_quad_vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        // B. Point Pass
        m_pointShader->use();
        float aspect_ratio = (float)width / (float)height;
        float zoom = config.getCameraZoom();
        float half_height = 2.0f / (zoom < 1e-6f ? 1e-6f : zoom);
        float half_width = half_height * aspect_ratio;
        glm::mat4 projection = glm::ortho(
            -half_width - config.getCameraX(), half_width - config.getCameraX(),
            -half_height - config.getCameraY(), half_height - config.getCameraY(),
            -1.0f, 1.0f
        );
        m_pointShader->setMat4("projection", projection);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE); // Use additive blending for a bright, fiery look
        glBindVertexArray(m_point_render_vao);
        glDrawArrays(GL_POINTS, 0, (GLsizei)config.getTotalPoints());

        // --- PART 2: Accumulation Pass for Denoising ---
        glBindFramebuffer(GL_FRAMEBUFFER, m_accumulation_fbo);
        m_quadShader->use();
        glDisable(GL_BLEND);
        m_quadShader->setFloat("u_blend_factor", config.getDenoiseFactor());
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_fbo_texture);
        m_quadShader->setInt("screenTexture", 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_accumulation_texture);
        m_quadShader->setInt("accumulationTexture", 1);
        glBindVertexArray(m_quad_vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    // --- PART 3: Final Display Pass (This runs every frame) ---
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (is_transparent) {
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    } else {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    }
    glClear(GL_COLOR_BUFFER_BIT);
    m_quadShader->use();
    m_quadShader->setFloat("u_blend_factor", 0.0f);
    m_quadShader->setVec2("u_resolution", {(float)width, (float)height});
    m_quadShader->setFloat("u_brightness", config.getBrightness());
    m_quadShader->setFloat("u_contrast", config.getContrast());
    m_quadShader->setFloat("u_gamma", config.getGamma());
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_accumulation_texture);
    m_quadShader->setInt("screenTexture", 0);
    glBindVertexArray(m_quad_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

// --- Private Helper Implementations ---

void Renderer::createShaders() {
    m_pointShader = std::make_unique<Shader>("shaders/vert/point.vert", "shaders/frag/point.frag");
    m_quadShader = std::make_unique<Shader>("shaders/vert/quad.vert", "shaders/frag/quad.frag");
    m_fadeShader = std::make_unique<Shader>("shaders/vert/quad.vert", "shaders/frag/fade.frag");
    m_computeShader = std::make_unique<Shader>("shaders/comp/fractal.comp");
}

void Renderer::createFramebuffers() {
    // Clean up old resources before creating new ones
    if (m_fbo) glDeleteFramebuffers(1, &m_fbo);
    if (m_fbo_texture) glDeleteTextures(1, &m_fbo_texture);
    if (m_accumulation_fbo) glDeleteFramebuffers(1, &m_accumulation_fbo);
    if (m_accumulation_texture) glDeleteTextures(1, &m_accumulation_texture);

    // --- Main FBO (for raw points and motion blur) ---
    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glGenTextures(1, &m_fbo_texture);
    glBindTexture(GL_TEXTURE_2D, m_fbo_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, m_width, m_height, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_fbo_texture, 0);

    // --- Accumulation FBO (for denoising) ---
    glGenFramebuffers(1, &m_accumulation_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_accumulation_fbo);
    glGenTextures(1, &m_accumulation_texture);
    glBindTexture(GL_TEXTURE_2D, m_accumulation_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, m_width, m_height, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_accumulation_texture, 0);
    
    glClear(GL_COLOR_BUFFER_BIT); // Clear the accumulation buffer initially
    
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "ERROR::FRAMEBUFFER:: Framebuffer is not complete!" << std::endl;
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::createScreenQuad() {
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

void Renderer::createGPUComputeBuffers(const Config& config) {
    if (m_transforms_ssbo == 0) glGenBuffers(1, &m_transforms_ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_transforms_ssbo);
    // Allocate a buffer large enough for a reasonable number of transforms.
    glBufferData(GL_SHADER_STORAGE_BUFFER, 100 * sizeof(Transform), nullptr, GL_DYNAMIC_DRAW);
    
    if (m_points_ssbo) glDeleteBuffers(1, &m_points_ssbo); // Delete old one if it exists
    glGenBuffers(1, &m_points_ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_points_ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER, config.getTotalPoints() * sizeof(Point), nullptr, GL_STATIC_DRAW);
}

void Renderer::generateFractalOnGPU(const Config& config, const std::vector<Transform>& transforms) {
    if (transforms.empty()) return;
    
    // Upload the latest interpolated transform data to the GPU
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_transforms_ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER, transforms.size() * sizeof(Transform), transforms.data(), GL_DYNAMIC_DRAW);
    
    m_computeShader->use();
    
    m_computeShader->setUint("num_transforms", (GLuint)transforms.size());
    m_computeShader->setUint("total_points", (GLuint)config.getTotalPoints());
    m_computeShader->setUint("u_warmup_iterations", config.getWarmupIterations());
    m_computeShader->setUint("u_main_iterations", config.getMainIterations());
    
    unsigned int current_seed = (config.getFractalSeed() == 0) ? m_rd() : config.getFractalSeed();
    m_computeShader->setUint("seed", current_seed);
    
    // Bind the SSBOs to the correct binding points (0 for transforms, 1 for points)
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_transforms_ssbo);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_points_ssbo);
    
    const unsigned int WORKGROUP_SIZE = 256;
    GLuint num_groups = (GLuint)(config.getTotalPoints() + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
    glDispatchCompute(num_groups, 1, 1);
    
    // Ensure that the compute shader finishes writing to the buffer before we try to render from it
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

