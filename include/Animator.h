#pragma once
#include "Config.h"
#include <vector>
#include <random>

class Animator {
public:
    Animator();

    void update(float delta_time, const Config& config);

    void reset(const Config& config);

    const std::vector<Transform>& getInterpolatedTransforms() const;

private:
    std::vector<Transform> generate_random_state(const Config& config);

    std::vector<Transform> m_previous_transforms;
    std::vector<Transform> m_target_transforms;
    std::vector<Transform> m_interpolated_transforms;

    int m_current_state_index = 0;
    int m_animation_direction = 1;
    float m_interpolation_alpha = 1.0f;

    std::random_device m_rd;
    std::mt19937 m_rd_generator;
};
