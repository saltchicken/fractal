#include "Application.h"
#include "Window.h"
#include "Renderer.h"
#include "Animator.h"
#include "cxxopts.hpp"
#include <iostream>
#include <algorithm> // Required for std::min
#include <GLFW/glfw3.h> // For glfwGetTime
#include <gtc/type_ptr.hpp> // Required for glm::mix

Application::Application(int argc, char* argv[]) {
    cxxopts::Options options("FractalFlame", "A GPU-accelerated fractal flame renderer");
    options.add_options()
        ("c,config", "Path to config INI", cxxopts::value<std::string>()->default_value("config.ini"))
        ("h,help", "Print usage");
    
    auto result = options.parse(argc, argv);
    if (result.count("help")) {
        std::cout << options.help() << std::endl;
        exit(0);
    }
    m_config_path = result["config"].as<std::string>();
    m_window = std::make_unique<Window>();
    m_renderer = std::make_unique<Renderer>();
    m_animator = std::make_unique<Animator>();
}

Application::~Application() = default;

void Application::loadConfig() {
    // Load into the target config first
    if (!m_target_config.load(m_config_path)) {
        throw std::runtime_error("Config load failed. Please check " + m_config_path);
    }
    if (m_target_config.getAnimationMode() != RANDOM && m_target_config.getStates().empty()) {
        throw std::runtime_error("No states found in config and not in random generation mode.");
    }
    m_last_config_write_time = std::filesystem::last_write_time(m_config_path);
    m_hot_reload_check_timer = HOT_RELOAD_INTERVAL;
    // Sync all configs at startup
    m_config = m_target_config;
    m_source_config = m_target_config;
    m_width = m_config.getWidth();
    m_height = m_config.getHeight();
}

void Application::run() {
    loadConfig();
    if (!m_window->init(m_width, m_height, "GPU Fractal Flame")) return;
    if (!m_renderer->init(m_config)) return;
    
    m_animator->reset(m_config);
    m_window->setResizeCallback([this](int width, int height) {
        this->handleWindowResize(width, height);
    });
    m_last_frame_time = glfwGetTime();
    while (!m_window->shouldClose()) {
        double current_time = glfwGetTime();
        float delta_time = static_cast<float>(current_time - m_last_frame_time);
        m_last_frame_time = current_time;
        m_window->pollEvents();
        m_window->processInput();
        m_hot_reload_check_timer -= delta_time;
        if (m_hot_reload_check_timer <= 0.0f) {
            check_for_config_updates();
            m_hot_reload_check_timer = HOT_RELOAD_INTERVAL;
        }
        // --- NEW: Live Parameter Interpolation ---
        if (m_param_interpolation_alpha < 1.0f) {
            m_param_interpolation_alpha += delta_time / PARAM_INTERPOLATION_DURATION;
            m_param_interpolation_alpha = std::min(m_param_interpolation_alpha, 1.0f);
            
            float alpha = m_param_interpolation_alpha; // for readability
            m_config.setBrightness(glm::mix(m_source_config.getBrightness(), m_target_config.getBrightness(), alpha));
            m_config.setContrast(glm::mix(m_source_config.getContrast(), m_target_config.getContrast(), alpha));
            m_config.setGamma(glm::mix(m_source_config.getGamma(), m_target_config.getGamma(), alpha));
            m_config.setPersistence(glm::mix(m_source_config.getPersistence(), m_target_config.getPersistence(), alpha));
            m_config.setDenoiseFactor(glm::mix(m_source_config.getDenoiseFactor(), m_target_config.getDenoiseFactor(), alpha));
            m_config.setCameraX(glm::mix(m_source_config.getCameraX(), m_target_config.getCameraX(), alpha));
            m_config.setCameraY(glm::mix(m_source_config.getCameraY(), m_target_config.getCameraY(), alpha));
            m_config.setCameraZoom(glm::mix(m_source_config.getCameraZoom(), m_target_config.getCameraZoom(), alpha));
        }
        m_animator->update(delta_time, m_config);
        
        m_renderer->render(m_config, m_animator->getInterpolatedTransforms(), m_width, m_height);
        m_window->swapBuffers();
    }
}

void Application::handleWindowResize(int width, int height) {
    m_width = width;
    m_height = height;
    m_renderer->onWindowResize(width, height);
}

void Application::check_for_config_updates() {
    try {
        auto current_write_time = std::filesystem::last_write_time(m_config_path);
        if (current_write_time > m_last_config_write_time) {
            m_last_config_write_time = current_write_time;
            std::cout << m_config_path << " changed, attempting to reload..." << std::endl;
            
            Config new_config;
            if (new_config.load(m_config_path)) {
                // Compare new_config against the current target
                bool points_changed = m_target_config.getTotalPoints() != new_config.getTotalPoints();
                bool animation_sequence_changed =
                    m_target_config.getAnimationMode() != new_config.getAnimationMode() ||
                    m_target_config.getStates().size() != new_config.getStates().size() ||
                    m_target_config.getFractalSeed() != new_config.getFractalSeed() ||
                    m_target_config.getInterpolationDuration() != new_config.getInterpolationDuration();
                
                unsigned int old_width = m_target_config.getWidth();
                if (animation_sequence_changed) {
                    std::cout << "Animation sequence change detected, resetting animator." << std::endl;
                    m_config = m_target_config = m_source_config = new_config;
                    m_param_interpolation_alpha = 1.0f; // Stop any live interpolation
                    
                    m_animator->reset(m_config);
                    m_renderer->resetGPUResources(m_config);
                } else if (points_changed) {
                    std::cout << "TotalPoints changed, resetting GPU resources." << std::endl;
                    m_config = m_target_config = m_source_config = new_config;
                    m_param_interpolation_alpha = 1.0f; // Stop any live interpolation
                    m_renderer->resetGPUResources(m_config);
                } else {
                    std::cout << "Live parameter change detected. Interpolating..." << std::endl;
                    m_source_config = m_config; // The current interpolated state is the source
                    m_target_config = new_config; // The new file is the target
                    m_param_interpolation_alpha = 0.0f; // Start the interpolation
                }
                
                if (old_width != new_config.getWidth() || m_height != new_config.getHeight()) {
                    glfwSetWindowSize(m_window->getNativeWindow(), new_config.getWidth(), new_config.getHeight());
                }
                std::cout << "Successfully reloaded " << m_config_path << "!" << std::endl;
            } else {
                std::cerr << "Failed to reload " << m_config_path << ", keeping old settings." << std::endl;
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error checking config file: " << e.what() << std::endl;
    }
}
