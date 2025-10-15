#include "Application.h"
#include "Window.h"
#include "Renderer.h"
#include "Animator.h"
#include "cxxopts.hpp"
#include <iostream>
#include <algorithm> // Required for std::min
#include <GLFW/glfw3.h> // For glfwGetTime and key codes
#include <gtc/type_ptr.hpp> // Required for glm::mix
#include <string>    // Add for std::to_string
#include <thread>    // Add for std::this_thread::sleep_for
#include <chrono>    // Add for std::chrono and timing
#include <vector>
#include <sstream>

Application::Application(int argc, char* argv[]) {
    cxxopts::Options options("FractalFlame", "A GPU-accelerated fractal flame renderer");
    options.add_options()
        ("c,config", "Path to config INI", cxxopts::value<std::string>()->default_value("config.ini"))
        ("t,transparent", "Enable transparent window background", cxxopts::value<bool>()->default_value("false"))
        ("h,help", "Print usage")
        ("video-output", "Render to video file (e.g., output.mp4)", cxxopts::value<std::string>())
        ("video-duration", "Video duration in seconds", cxxopts::value<float>()->default_value("10.0"))
        ("video-fps", "Video framerate", cxxopts::value<unsigned int>()->default_value("60"));
    
    auto result = options.parse(argc, argv);
    if (result.count("help")) {
        std::cout << options.help() << std::endl;
        exit(0);
    }
    m_config_path = result["config"].as<std::string>();
    m_is_transparent = result["transparent"].as<bool>();
    if (result.count("video-output")) {
        m_is_video_render_mode = true;
        m_video_output_path = result["video-output"].as<std::string>();
        m_video_duration = result["video-duration"].as<float>();
        m_video_fps = result["video-fps"].as<unsigned int>();
        m_is_transparent = false; // Video rendering doesn't support transparency
        std::cout << "VIDEO RENDER MODE" << std::endl;
        std::cout << "Output: " << m_video_output_path << ", Duration: " << m_video_duration << "s, FPS: " << m_video_fps << std::endl;
    }
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
    unsigned int target_fps = m_is_video_render_mode ? m_video_fps : m_config.getTargetFPS();
    if (target_fps > 0) {
        m_target_frame_time = 1.0 / static_cast<double>(target_fps);
    } else {
        m_target_frame_time = 0.0;
    }
}
void Application::run() {
    loadConfig();
    if (!m_window->init(m_width, m_height, "GPU Fractal Flame", m_is_transparent, !m_is_video_render_mode)) return;
    if (!m_renderer->init(m_config)) return;
    
    m_animator->reset(m_config);
    m_window->setResizeCallback([this](int width, int height) {
        this->handleWindowResize(width, height);
    });
    m_window->setKeyCallback([this](int key, int scancode, int action, int mods) {
        this->onKey(key, scancode, action, mods);
    });
    
    if (m_is_video_render_mode) {
        startVideoRender();
    }
    m_last_frame_time = glfwGetTime();
    while (!m_window->shouldClose()) {
        double current_time = glfwGetTime();
        float delta_time = m_is_video_render_mode ? (1.0f / m_video_fps) : static_cast<float>(current_time - m_last_frame_time);
        m_last_frame_time = current_time;
        if (!m_is_video_render_mode) {
             m_fps_timer += delta_time;
             m_frame_counter++;
             if (m_fps_timer >= 1.0) {
                 std::cout << "FPS: " << m_frame_counter << std::endl;
                 m_frame_counter = 0;
                 m_fps_timer = fmod(m_fps_timer, 1.0);
             }
        }
        m_window->pollEvents();
        
        if (!m_is_video_render_mode) {
            m_hot_reload_check_timer -= delta_time;
            if (m_hot_reload_check_timer <= 0.0f) {
                check_for_config_updates();
                m_hot_reload_check_timer = HOT_RELOAD_INTERVAL;
            }
        }
        
        if (m_param_interpolation_alpha < 1.0f) {
            m_param_interpolation_alpha += delta_time / PARAM_INTERPOLATION_DURATION;
            m_param_interpolation_alpha = std::min(m_param_interpolation_alpha, 1.0f);
            
            float alpha = m_param_interpolation_alpha;
            m_config.setBrightness(glm::mix(m_source_config.getBrightness(), m_target_config.getBrightness(), alpha));
            m_config.setContrast(glm::mix(m_source_config.getContrast(), m_target_config.getContrast(), alpha));
            m_config.setGamma(glm::mix(m_source_config.getGamma(), m_target_config.getGamma(), alpha));
            m_config.setPersistence(glm::mix(m_source_config.getPersistence(), m_target_config.getPersistence(), alpha));
            m_config.setDenoiseFactor(glm::mix(m_source_config.getDenoiseFactor(), m_target_config.getDenoiseFactor(), alpha));
            m_config.setCameraX(glm::mix(m_source_config.getCameraX(), m_target_config.getCameraX(), alpha));
            m_config.setCameraY(glm::mix(m_source_config.getCameraY(), m_target_config.getCameraY(), alpha));
            m_config.setCameraZoom(glm::mix(m_source_config.getCameraZoom(), m_target_config.getCameraZoom(), alpha));
        }
        
        if (!m_is_paused || m_is_video_render_mode) {
             m_animator->update(delta_time, m_config);
        }
        
        m_renderer->render(m_config, m_animator->getInterpolatedTransforms(), m_width, m_height, m_is_transparent, m_is_paused);
        
        if (m_is_video_render_mode) {
            writeVideoFrame();
            printf("Rendering video: %.2f / %.2f seconds (%.1f%%)\r", m_video_time_elapsed, m_video_duration, (m_video_time_elapsed / m_video_duration) * 100.0f);
            fflush(stdout);
            if (m_video_time_elapsed >= m_video_duration) {
                break;
            }
        } else {
            m_window->swapBuffers();
        }
        
        if (m_target_frame_time > 0.0 && !m_is_video_render_mode) {
            double frame_end_time = glfwGetTime();
            double time_to_wait = m_target_frame_time - (frame_end_time - current_time);
            if (time_to_wait > 0) {
                if (time_to_wait > 0.002) {
                    std::this_thread::sleep_for(std::chrono::duration<double>(time_to_wait - 0.0015));
                }
                while (glfwGetTime() - current_time < m_target_frame_time) {
                    std::this_thread::yield();
                }
            }
        }
    }
    if (m_is_video_render_mode) {
        endVideoRender();
    }
}
void Application::onKey(int key, int scancode, int action, int mods) {
    if (action == GLFW_PRESS) {
        if (key == GLFW_KEY_ESCAPE) {
            glfwSetWindowShouldClose(m_window->getNativeWindow(), true);
        } else if (key == GLFW_KEY_SPACE && !m_is_video_render_mode) {
            m_is_paused = !m_is_paused;
            std::cout << "Animation " << (m_is_paused ? "paused" : "resumed") << std::endl;
        }
    }
}
void Application::startVideoRender() {
    std::stringstream cmd;
    cmd << "ffmpeg -y -f rawvideo -vcodec rawvideo -pix_fmt rgba -s " << m_width << "x" << m_height
        << " -r " << m_video_fps << " -i - -vf vflip -c:v libx264 -preset medium -crf 20 -pix_fmt yuv420p " << m_video_output_path;
#ifdef _WIN32
    m_ffmpeg_pipe = _popen(cmd.str().c_str(), "wb");
#else
    m_ffmpeg_pipe = popen(cmd.str().c_str(), "w");
#endif
    if (!m_ffmpeg_pipe) {
        throw std::runtime_error("Failed to open pipe to FFmpeg. Is it installed and in your PATH?");
    }
    m_video_frame_buffer.resize(m_width * m_height * 4);
}
void Application::writeVideoFrame() {
    // Read pixels from the back buffer. In video mode, we don't call swapBuffers,
    // so the completed frame is always in the back buffer.
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, m_width, m_height, GL_RGBA, GL_UNSIGNED_BYTE, m_video_frame_buffer.data());
    fwrite(m_video_frame_buffer.data(), 1, m_video_frame_buffer.size(), m_ffmpeg_pipe);
}
void Application::endVideoRender() {
    if (m_ffmpeg_pipe) {
#ifdef _WIN32
        _pclose(m_ffmpeg_pipe);
#else
        pclose(m_ffmpeg_pipe);
#endif
        std::cout << "\nVideo render finished: " << m_video_output_path << std::endl;
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
                unsigned int new_target_fps = new_config.getTargetFPS();
                if (new_target_fps > 0) {
                    m_target_frame_time = 1.0 / static_cast<double>(new_target_fps);
                } else {
                    m_target_frame_time = 0.0;
                }
                bool points_changed = m_target_config.getTotalPoints() != new_config.getTotalPoints();
                bool animation_sequence_changed =
                    m_target_config.getAnimationMode() != new_config.getAnimationMode() ||
                    m_target_config.getStates() != new_config.getStates() ||
                    m_target_config.getFractalSeed() != new_config.getFractalSeed();
                
                bool randomization_params_changed =
                    m_target_config.getRandomNumTransforms() != new_config.getRandomNumTransforms() ||
                    m_target_config.getRandomVariations() != new_config.getRandomVariations() ||
                    m_target_config.getRandomColorMode() != new_config.getRandomColorMode() ||
                    m_target_config.getRandomPalette() != new_config.getRandomPalette() ||
                    m_target_config.getRandomColorRangeR() != new_config.getRandomColorRangeR() ||
                    m_target_config.getRandomColorRangeG() != new_config.getRandomColorRangeG() ||
                    m_target_config.getRandomColorRangeB() != new_config.getRandomColorRangeB();
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
                    m_config.setInterpolationDuration(new_config.getInterpolationDuration()); 
                    m_config.setWarmupIterations(new_config.getWarmupIterations());
                    m_config.setMainIterations(new_config.getMainIterations());
                    m_config.setTargetFPS(new_config.getTargetFPS());
                    if (randomization_params_changed) {
                        std::cout << "Randomization settings will update on next cycle." << std::endl;
                        m_config.setRandomNumTransforms(new_config.getRandomNumTransforms());
                        m_config.setRandomVariations(new_config.getRandomVariations());
                        m_config.setRandomColorMode(new_config.getRandomColorMode());
                        m_config.setRandomPalette(new_config.getRandomPalette());
                        m_config.setRandomColorRangeR(new_config.getRandomColorRangeR());
                        m_config.setRandomColorRangeG(new_config.getRandomColorRangeG());
                        m_config.setRandomColorRangeB(new_config.getRandomColorRangeB());
                    }
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
