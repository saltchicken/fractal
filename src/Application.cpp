#include "Application.h"
#include "Window.h"
#include "Renderer.h"
#include "Animator.h"
#include "cxxopts.hpp"
#include <iostream>
#include <GLFW/glfw3.h> // For glfwGetTime

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
    if (!m_config.load(m_config_path)) {
        throw std::runtime_error("Config load failed. Please check " + m_config_path);
    }
    if (m_config.getAnimationMode() != RANDOM && m_config.getStates().empty()) {
        throw std::runtime_error("No states found in config and not in random generation mode.");
    }
    m_last_config_write_time = std::filesystem::last_write_time(m_config_path);
    m_hot_reload_check_timer = HOT_RELOAD_INTERVAL;
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
                m_config = new_config;
                
                m_animator->reset(m_config);
                
                if (m_width != m_config.getWidth() || m_height != m_config.getHeight()) {
                    glfwSetWindowSize(m_window->getNativeWindow(), m_config.getWidth(), m_config.getHeight());
                }
                
                m_renderer->resetGPUResources(m_config);
                
                std::cout << "Successfully reloaded " << m_config_path << "!" << std::endl;
            } else {
                std::cerr << "Failed to reload " << m_config_path << ", keeping old settings." << std::endl;
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error checking config file: " << e.what() << std::endl;
    }
}
