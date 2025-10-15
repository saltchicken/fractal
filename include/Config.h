#pragma once
#include <string>
#include <vector>
#include <glm.hpp>
#include <random>

// Configuration-related data structures
enum Variation { LINEAR, SINUSOIDAL, SPHERICAL, SWIRL, HORSESHOE,
                 POLAR, HEART, DISK, SPIRAL, HYPERBOLIC,
                 JULIA, BENT, FISHEYE, POPCORN, DIAMOND,
                 VARIATION_COUNT
};

struct Transform {
    // Maps to affine transform:
    // x' = params1.x * x + params1.y * y + params1.z  (a, b, c)
    // y' = params1.w * x + params2.x * y + params2.y  (d, e, f)
    glm::vec4 params1{};
    glm::vec4 params2{};
    glm::vec4 color{};
    glm::uvec4 variation{};

    bool operator==(const Transform& other) const {
        return params1 == other.params1 &&
               params2 == other.params2 &&
               color == other.color &&
               variation == other.variation;
    }
};

enum AnimationMode { PING_PONG, LOOP, RANDOM, BOUNCE };
enum RandomColorMode { RCM_RANDOM, RCM_PALETTE };

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
    float getBrightness() const { return post_brightness; }
    float getContrast() const { return post_contrast; }
    float getGamma() const { return post_gamma; }
    float getPersistence() const { return post_persistence; }
    float getDenoiseFactor() const { return post_denoise_factor; }
    long long getTotalPoints() const { return total_points; }
    float getInterpolationDuration() const { return interpolation_duration; }
    unsigned int getFractalSeed() const { return fractal_seed; }
    AnimationMode getAnimationMode() const { return animation_mode; }
    unsigned int getWarmupIterations() const { return warmup_iterations; }
    unsigned int getMainIterations() const { return main_iterations; }
    float getPointAlpha() const { return point_alpha; }
    unsigned int getTargetFPS() const { return target_fps; }
    const std::vector<std::vector<Transform>>& getStates() const { return states; }

    const glm::ivec2& getRandomNumTransforms() const { return random_num_transforms; }
    const std::vector<Variation>& getRandomVariations() const { return random_variations; }
    RandomColorMode getRandomColorMode() const { return random_color_mode; }
    const std::vector<glm::vec3>& getRandomPalette() const { return random_palette; }
    const glm::vec2& getRandomColorRangeR() const { return random_color_range_r; }
    const glm::vec2& getRandomColorRangeG() const { return random_color_range_g; }
    const glm::vec2& getRandomColorRangeB() const { return random_color_range_b; }

    void setCameraX(float val) { camera_x = val; }
    void setCameraY(float val) { camera_y = val; }
    void setCameraZoom(float val) { camera_zoom = val; }
    void setBrightness(float val) { post_brightness = val; }
    void setContrast(float val) { post_contrast = val; }
    void setGamma(float val) { post_gamma = val; }
    void setPersistence(float val) { post_persistence = val; }
    void setDenoiseFactor(float val) { post_denoise_factor = val; }
    void setInterpolationDuration(float val) { interpolation_duration = val; }
    void setWarmupIterations(unsigned int val) { warmup_iterations = val; }
    void setMainIterations(unsigned int val) { main_iterations = val; }
    void setTargetFPS(unsigned int val) { target_fps = val; }
    void setRandomNumTransforms(const glm::ivec2& val) { random_num_transforms = val; }
    void setRandomVariations(const std::vector<Variation>& val) { random_variations = val; }
    void setRandomColorMode(RandomColorMode val) { random_color_mode = val; }
    void setRandomPalette(const std::vector<glm::vec3>& val) { random_palette = val; }
    void setRandomColorRangeR(const glm::vec2& val) { random_color_range_r = val; }
    void setRandomColorRangeG(const glm::vec2& val) { random_color_range_g = val; }
    void setRandomColorRangeB(const glm::vec2& val) { random_color_range_b = val; }


private:
    // Settings
    unsigned int width = 1280;
    unsigned int height = 720;
    float camera_x = 0.0f;
    float camera_y = 0.0f;
    float camera_zoom = 1.0f;
    float post_brightness = 1.0f;
    float post_contrast = 1.0f;
    float post_gamma = 1.2f;
    float post_persistence = 0.95f;
    float post_denoise_factor = 0.05f;
    long long total_points = 500000;
    float interpolation_duration = 2.0f;
    unsigned int fractal_seed = 0;
    AnimationMode animation_mode = PING_PONG;
    unsigned int warmup_iterations = 20;
    unsigned int main_iterations = 150;
    float point_alpha = 0.15f;
    unsigned int target_fps = 0;

    // Parsed fractal states
    std::vector<std::vector<Transform>> states;

    glm::ivec2 random_num_transforms{2, 2};
    std::vector<Variation> random_variations;
    RandomColorMode random_color_mode = RCM_PALETTE;
    std::vector<glm::vec3> random_palette;
    glm::vec2 random_color_range_r{0.0f, 1.0f};
    glm::vec2 random_color_range_g{0.0f, 1.0f};
    glm::vec2 random_color_range_b{0.0f, 1.0f};

    // Random number generator for parsing random() values
    std::mt19937 rng;

    // Private helper methods used by the INI handler
    float parse_float_or_random(const std::string& value_str);
    void handle_settings(const std::string& name, const std::string& value);
    void handle_camera(const std::string& name, const std::string& value);
    void handle_post_processing(const std::string& name, const std::string& value);
    void handle_randomization(const std::string& name, const std::string& value);
    void handle_transform(const std::string& section, const std::string& name, const std::string& value);

    // The static callback for ini_parse
    static int handler(void* user, const char* section, const char* name, const char* value);
};
