#include "Animator.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <map>
#include <string>

namespace {
    // Helper to convert Variation enum to string for logging
    std::string variation_to_string(Variation var) {
        static const std::map<Variation, std::string> reverse_variation_map = {
            {LINEAR, "LINEAR"}, {SINUSOIDAL, "SINUSOIDAL"}, {SPHERICAL, "SPHERICAL"},
            {SWIRL, "SWIRL"}, {HORSESHOE, "HORSESHOE"}
        };
        auto it = reverse_variation_map.find(var);
        if (it != reverse_variation_map.end()) {
            return it->second;
        }
        return "UNKNOWN";
    }
}

Animator::Animator() : m_rd_generator(m_rd()) {}

void Animator::reset(const Config& config) {
    m_interpolation_alpha = 1.0f;
    m_animation_direction = 1;
    if (config.getAnimationMode() == RANDOM) {
        m_target_transforms = generate_random_state(config);
    } else if (!config.getStates().empty()) {
        m_current_state_index = std::min(m_current_state_index, (int)config.getStates().size() - 1);
        m_current_state_index = std::max(0, m_current_state_index);
        m_target_transforms = config.getStates()[m_current_state_index];
    } else {
        m_target_transforms.clear();
    }
    m_previous_transforms = m_target_transforms;
}

void Animator::update(float delta_time, const Config& config) {
    // Animation interpolation logic
    if (config.getStates().size() > 1 || config.getAnimationMode() == RANDOM) {
        m_interpolation_alpha += delta_time / config.getInterpolationDuration();
        if (m_interpolation_alpha >= 1.0f) {
            m_interpolation_alpha = fmod(m_interpolation_alpha, 1.0f);
            if (config.getAnimationMode() == RANDOM) {
                m_previous_transforms = m_target_transforms;
                m_target_transforms = generate_random_state(config);
                std::cout << "Animating to new random state..." << std::endl;
            } else {
                m_previous_transforms = config.getStates()[m_current_state_index];
                const auto& states = config.getStates();
                if (config.getAnimationMode() == PING_PONG) {
                    int next_idx = m_current_state_index + m_animation_direction;
                    if (next_idx >= (int)states.size() || next_idx < 0) {
                        m_animation_direction *= -1;
                        next_idx = m_current_state_index + m_animation_direction;
                    }
                    m_current_state_index = next_idx;
                } else if (config.getAnimationMode() == LOOP) {
                    m_current_state_index = (m_current_state_index + 1) % states.size();
                } else if (config.getAnimationMode() == BOUNCE) {
                    if (states.size() > 1) {
                        std::uniform_int_distribution<int> dist(0, (int)states.size() - 1);
                        int next_idx = m_current_state_index;
                        while (next_idx == m_current_state_index) {
                            next_idx = dist(m_rd_generator);
                        }
                        m_current_state_index = next_idx;
                    }
                }
                m_target_transforms = states[m_current_state_index];
                std::cout << "Animating to state " << (m_current_state_index + 1) << "..." << std::endl;
            }
        }
    } else {
        m_interpolation_alpha = 1.0f;
    }

    // Calculate Interpolated Transforms
    m_interpolated_transforms.clear();
    size_t num_target = m_target_transforms.size();
    size_t num_previous = m_previous_transforms.size();
    size_t render_list_size = std::max(num_target, num_previous);
    m_interpolated_transforms.reserve(render_list_size);

    for (size_t i = 0; i < render_list_size; ++i) {
        bool is_appearing = (i >= num_previous);
        bool is_disappearing = (i >= num_target);
        Transform prev, target;

        if (is_disappearing) {
            prev = m_previous_transforms[i];
            target = m_previous_transforms[i];
            target.color.a = 0.0f;
        } else if (is_appearing) {
            target = m_target_transforms[i];
            prev = m_target_transforms[i];
            prev.color.a = 0.0f;
        } else {
            prev = m_previous_transforms[i];
            target = m_target_transforms[i];
        }

        Transform interpolated;
        interpolated.params1 = glm::mix(prev.params1, target.params1, m_interpolation_alpha);
        interpolated.params2 = glm::mix(prev.params2, target.params2, m_interpolation_alpha);
        interpolated.color = glm::mix(prev.color, target.color, m_interpolation_alpha);
        interpolated.variation = (m_interpolation_alpha < 0.5f) ? prev.variation : target.variation;

        m_interpolated_transforms.push_back(interpolated);
    }
}

const std::vector<Transform>& Animator::getInterpolatedTransforms() const {
    return m_interpolated_transforms;
}

std::vector<Transform> Animator::generate_random_state(const Config& config) {
    auto rand_float = [this](float min, float max) {
        std::uniform_real_distribution<float> dist(min, max);
        return dist(m_rd_generator);
    };

    const auto& config_states = config.getStates();
    std::vector<Transform> new_state;
    std::string generation_method;

    // Primary logic: Use State.1 from the config as a template if it exists.
    if (!config_states.empty() && !config_states[0].empty()) {
        generation_method = "from State.1 template";
        const auto& template_state = config_states[0];
        new_state.reserve(template_state.size());

        for (const auto& t_template : template_state) {
            Transform t;
            t.params1 = glm::vec4(rand_float(-1.2f, 1.2f), rand_float(-1.2f, 1.2f), rand_float(-1.2f, 1.2f), rand_float(-1.2f, 1.2f));
            t.params2 = glm::vec4(rand_float(-1.2f, 1.2f), rand_float(-1.2f, 1.2f), 0.f, 0.f);
            t.color = glm::vec4(rand_float(0.f, 1.f), rand_float(0.f, 1.f), rand_float(0.f, 1.f), 0.15f);
            t.variation = t_template.variation; // Keep the variation from the template
            new_state.push_back(t);
        }
    } else { // --- Fallback Logic ---
        generation_method = "fully random";
        std::uniform_int_distribution<int> num_dist(2, 4);
        int num_transforms = num_dist(m_rd_generator);
        new_state.reserve(num_transforms);

        auto rand_variation = [this]() {
            std::uniform_int_distribution<int> dist(0, HORSESHOE);
            return static_cast<Variation>(dist(m_rd_generator));
        };
        for (int i = 0; i < num_transforms; ++i) {
            Transform t;
            t.params1.x = rand_float(-1.2f, 1.2f);
            t.params1.y = rand_float(-1.2f, 1.2f);
            t.params1.z = rand_float(-1.2f, 1.2f);
            t.params1.w = rand_float(-1.2f, 1.2f);
            t.params2.x = rand_float(-1.2f, 1.2f);
            t.params2.y = rand_float(-1.2f, 1.2f);
            glm::vec3 color_vec(rand_float(0.0f, 1.0f), rand_float(0.0f, 1.0f), rand_float(0.0f, 1.0f));
            t.color = glm::vec4(color_vec, 0.15f);
            t.variation.x = rand_variation();
            new_state.push_back(t);
        }
    }

    // Common logging for both cases to display all parameters
    std::cout << "Generated new " << generation_method << " state with " << new_state.size() << " transforms:" << std::endl;
    for (size_t i = 0; i < new_state.size(); ++i) {
        const auto& t = new_state[i];
        std::cout << "  Transform " << i + 1 << ":" << std::endl;
        std::cout << "    a=" << t.params1.x << ", b=" << t.params1.y << ", c=" << t.params1.z
                  << ", d=" << t.params1.w << ", e=" << t.params2.x << ", f=" << t.params2.y << std::endl;
        std::cout << "    color=(" << t.color.r << ", " << t.color.g << ", " << t.color.b << ")" << std::endl;
        std::cout << "    variation=" << variation_to_string(static_cast<Variation>(t.variation.x)) << std::endl;
    }

    return new_state;
}
