#pragma once

#include <string>
#include <vector>
#include <glm.hpp>

// Configuration-related data structures
enum Variation { LINEAR, SINUSOIDAL, SPHERICAL, SWIRL, HORSESHOE };

struct Transform {
    glm::vec4 params1{};
    glm::vec4 params2{};
    glm::vec4 color{};
    glm::uvec4 variation{};
};

enum AnimationMode { PING_PONG, LOOP, RANDOM };

// A class to load and hold all configuration data from config.ini
class Config {
public:
    // Settings
    unsigned int width = 1280;
    unsigned int height = 720;
    long long total_points = 500000;
    float interpolation_duration = 2.0f;
    unsigned int fractal_seed = 0;
    AnimationMode animation_mode = PING_PONG;

    // Parsed fractal states
    std::vector<std::vector<Transform>> states;

    // Loads settings and states from the given filename.
    // Returns true on success, false on failure.
    bool load(const std::string& filename);
};
