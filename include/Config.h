#pragma once

#include <string>
#include <vector>
#include <glm.hpp>
#include <random>

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
    Config(); // Constructor for initializing RNG

    // Loads settings and states from the given filename.
    // Returns true on success, false on failure.
    bool load(const std::string& filename);

    // Accessors
    unsigned int getWidth() const { return width; }
    unsigned int getHeight() const { return height; }
    float getCameraX() const { return camera_x; }
    float getCameraY() const { return camera_y; }
    float getCameraZoom() const { return camera_zoom; }
    long long getTotalPoints() const { return total_points; }
    float getInterpolationDuration() const { return interpolation_duration; }
    unsigned int getFractalSeed() const { return fractal_seed; }
    AnimationMode getAnimationMode() const { return animation_mode; }
    const std::vector<std::vector<Transform>>& getStates() const { return states; }

private:
    // Settings
    unsigned int width = 1280;
    unsigned int height = 720;
    float camera_x = 0.0f;
    float camera_y = 0.0f;
    float camera_zoom = 1.0f;
    long long total_points = 500000;
    float interpolation_duration = 2.0f;
    unsigned int fractal_seed = 0;
    AnimationMode animation_mode = PING_PONG;

    // Parsed fractal states
    std::vector<std::vector<Transform>> states;

    // Random number generator for parsing random() values
    std::mt19937 rng;

    // Private helper methods used by the INI handler
    float parse_float_or_random(const std::string& value_str);
    void handle_settings(const std::string& name, const std::string& value);
    void handle_camera(const std::string& name, const std::string& value); // Add this line
    void handle_transform(const std::string& section, const std::string& name, const std::string& value);

    // The static callback for ini_parse
    static int handler(void* user, const char* section, const char* name, const char* value);
};
