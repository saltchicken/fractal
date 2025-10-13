#include "Config.h"
#define INI_H_IMPLEMENTATION
#include "ini.h"
#include <iostream>
#include <sstream>
#include <map>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace {
    // Helper function to trim whitespace from both ends of a string
    void trim(std::string &s) {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
            return !std::isspace(ch);
        }));
        s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
            return !std::isspace(ch);
        }).base(), s.end());
    }
}

// Map to convert string variations from the config file to the Variation enum
const std::map<std::string, Variation> variation_map = {
    {"LINEAR", LINEAR}, {"SINUSOIDAL", SINUSOIDAL}, {"SPHERICAL", SPHERICAL},
    {"SWIRL", SWIRL}, {"HORSESHOE", HORSESHOE}
};

Config::Config() : rng(std::random_device{}()) {}

float Config::parse_float_or_random(const std::string& value_str) {
    std::string s = value_str;
    trim(s);
    if (s.rfind("random", 0) == 0) {
        size_t open_paren = s.find('(');
        size_t close_paren = s.find(')');
        if (open_paren != std::string::npos && close_paren != std::string::npos) {
            std::string args = s.substr(open_paren + 1, close_paren - open_paren - 1);
            trim(args);
            if (args.empty()) {
                std::uniform_real_distribution<float> dist(0.0f, 1.0f);
                return dist(rng);
            } else {
                size_t comma_pos = args.find(',');
                if (comma_pos != std::string::npos) {
                    try {
                        float min_val = std::stof(args.substr(0, comma_pos));
                        float max_val = std::stof(args.substr(comma_pos + 1));
                        std::uniform_real_distribution<float> dist(min_val, max_val);
                        return dist(rng);
                    } catch (const std::exception&) {
                        std::cerr << "Warning: Could not parse random() arguments: " << args << ". Using 0.0f." << std::endl;
                        return 0.0f;
                    }
                }
            }
        }
        std::cerr << "Warning: Malformed random() call: " << s << ". Using 0.0f." << std::endl;
        return 0.0f;
    }
    try {
        return std::stof(s);
    } catch (const std::exception&) {
        std::cerr << "Warning: Could not parse float value: " << s << ". Using 0.0f." << std::endl;
        return 0.0f;
    }
}

void Config::handle_settings(const std::string& name, const std::string& value) {
    if (name == "Width") width = std::stoul(value);
    else if (name == "Height") height = std::stoul(value);
    else if (name == "TotalPoints") total_points = std::stoll(value);
    else if (name == "InterpolationDuration") interpolation_duration = std::stof(value);
    else if (name == "Seed") {
        fractal_seed = std::stoul(value);
        if (fractal_seed != 0) {
            rng.seed(fractal_seed); // Reseed the generator
        }
    } else if (name == "AnimationMode") {
        std::string mode_str = value;
        std::transform(mode_str.begin(), mode_str.end(), mode_str.begin(), ::tolower);
        if (mode_str == "loop") animation_mode = LOOP;
        else if (mode_str == "random") animation_mode = RANDOM;
        else animation_mode = PING_PONG;
    }
}

void Config::handle_camera(const std::string& name, const std::string& value) {
    if (name == "x") camera_x = std::stof(value);
    else if (name == "y") camera_y = std::stof(value);
    else if (name == "zoom") camera_zoom = std::stof(value);
}

void Config::handle_post_processing(const std::string& name, const std::string& value) {
    if (name == "Brightness") post_brightness = std::stof(value);
    else if (name == "Contrast") post_contrast = std::stof(value);
    else if (name == "Gamma") post_gamma = std::stof(value);
}

void Config::handle_transform(const std::string& section, const std::string& name, const std::string& value) {
    int state_num, transform_num;
    if (sscanf(section.c_str(), "State.%d.Transform.%d", &state_num, &transform_num) != 2) return;
    if (state_num <= 0 || transform_num <= 0) return; // Indices must be 1-based

    if (state_num > states.size()) states.resize(state_num);
    std::vector<Transform>& current_transforms = states[state_num - 1];
    if (transform_num > current_transforms.size()) current_transforms.resize(transform_num);

    Transform& t = current_transforms[transform_num - 1];

    if (name == "a") t.params1.x = parse_float_or_random(value);
    else if (name == "b") t.params1.y = parse_float_or_random(value);
    else if (name == "c") t.params1.z = parse_float_or_random(value);
    else if (name == "d") t.params1.w = parse_float_or_random(value);
    else if (name == "e") t.params2.x = parse_float_or_random(value);
    else if (name == "f") t.params2.y = parse_float_or_random(value);
    else if (name == "color") {
        glm::vec3 color_vec;
        std::stringstream ss(value);
        std::string component;
        std::getline(ss, component, ','); color_vec.r = parse_float_or_random(component);
        std::getline(ss, component, ','); color_vec.g = parse_float_or_random(component);
        std::getline(ss, component);       color_vec.b = parse_float_or_random(component);
        t.color = glm::vec4(color_vec, 0.15f);
    } else if (name == "variation") {
        std::string var_str = value;
        trim(var_str);
        if (var_str == "RANDOM") {
            std::uniform_int_distribution<int> dist(0, HORSESHOE);
            t.variation.x = static_cast<Variation>(dist(rng));
        } else if (variation_map.count(var_str)) {
            t.variation.x = variation_map.at(var_str);
        }
    }
}

int Config::handler(void* user, const char* section, const char* name, const char* value) {
    Config* pconfig = static_cast<Config*>(user);
    if (strcmp(section, "Settings") == 0) {
        pconfig->handle_settings(name, value);
    } else if (strcmp(section, "Camera") == 0) {
        pconfig->handle_camera(name, value);
    } else if (strcmp(section, "PostProcessing") == 0) { // Add this else if block
        pconfig->handle_post_processing(name, value);
    } else if (strncmp(section, "State.", 6) == 0) {
        pconfig->handle_transform(section, name, value);
    }
    return 1; // Success
}

bool Config::load(const std::string& filename) {
    states.clear();
    rng.seed(std::random_device{}());

    if (ini_parse(filename.c_str(), handler, this) < 0) {
        std::cerr << "Failed to load " << filename << std::endl;
        return false;
    }

    // Remove any empty states that might have been created if the user
    // defines states out of order (e.g., State.1 and State.3 but not State.2)
    states.erase(
        std::remove_if(states.begin(), states.end(),
                       [](const std::vector<Transform>& s) { return s.empty(); }),
        states.end()
    );

    return true;
}
