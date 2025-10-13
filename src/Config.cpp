#include "Config.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <map>
#include <algorithm>
#include <cctype>
#include <random> // New include for random number generation

// Anonymous namespace to hold the helper functions, keeping them private to this file.
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

    // New helper function to parse a string that could be a float or a random value.
    float parse_float_or_random(const std::string& value_str, std::mt19937& rng) {
        std::string s = value_str;
        trim(s);
        // Check if the string starts with "random"
        if (s.rfind("random", 0) == 0) {
            size_t open_paren = s.find('(');
            size_t close_paren = s.find(')');

            if (open_paren != std::string::npos && close_paren != std::string::npos) {
                // Extract arguments between parentheses
                std::string args = s.substr(open_paren + 1, close_paren - open_paren - 1);
                trim(args);

                if (args.empty()) {
                    // No arguments: random() -> returns float between 0.0 and 1.0
                    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
                    return dist(rng);
                } else {
                    // Two arguments: random(min, max)
                    size_t comma_pos = args.find(',');
                    if (comma_pos != std::string::npos) {
                        try {
                            float min_val = std::stof(args.substr(0, comma_pos));
                            float max_val = std::stof(args.substr(comma_pos + 1));
                            std::uniform_real_distribution<float> dist(min_val, max_val);
                            return dist(rng);
                        } catch (const std::exception& e) {
                            std::cerr << "Warning: Could not parse random() arguments: " << args << ". Using 0.0f. " << e.what() << std::endl;
                            return 0.0f;
                        }
                    }
                }
            }
            // Fallback for malformed random() call
            std::cerr << "Warning: Malformed random() call: " << s << ". Using 0.0f." << std::endl;
            return 0.0f;
        }
        // If not "random", parse as a regular float
        try {
            return std::stof(s);
        } catch (const std::exception& e) {
            std::cerr << "Warning: Could not parse float value: " << s << ". Using 0.0f. " << e.what() << std::endl;
            return 0.0f;
        }
    }
}

// Map to convert string variations from the config file to the Variation enum
const std::map<std::string, Variation> variation_map = {
    {"LINEAR", LINEAR}, {"SINUSOIDAL", SINUSOIDAL}, {"SPHERICAL", SPHERICAL},
    {"SWIRL", SWIRL}, {"HORSESHOE", HORSESHOE}
};

bool Config::load(const std::string& filename) {
    states.clear();
    
    // Create a random number generator for this load operation.
    std::random_device rd;
    std::mt19937 rng(rd());

    // --- INI parsing logic is now integrated here ---
    std::map<std::string, std::map<std::string, std::string>> config_data;
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to load " << filename << std::endl;
        return false;
    }

    std::string line;
    std::string current_section;
    while (std::getline(file, line)) {
        trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') {
            continue; // Skip comments and empty lines
        }
        if (line[0] == '[' && line.back() == ']') {
            current_section = line.substr(1, line.length() - 2);
            trim(current_section);
        } else {
            size_t equals_pos = line.find('=');
            if (equals_pos != std::string::npos) {
                std::string key = line.substr(0, equals_pos);
                std::string value = line.substr(equals_pos + 1);
                trim(key);
                trim(value);
                if (!current_section.empty()) {
                    config_data[current_section][key] = value;
                }
            }
        }
    }

    // --- End of integrated INI parsing ---

    std::map<int, std::vector<Transform>> state_map;

    try {
        if (config_data.count("Settings")) {
            const auto& settings = config_data.at("Settings");
            if (settings.count("Width")) width = std::stoi(settings.at("Width"));
            if (settings.count("Height")) height = std::stoi(settings.at("Height"));
            if (settings.count("InterpolationDuration")) interpolation_duration = std::stof(settings.at("InterpolationDuration"));
            if (settings.count("TotalPoints")) total_points = std::stoll(settings.at("TotalPoints"));
            if (settings.count("Seed")) fractal_seed = std::stoul(settings.at("Seed"));
            
            // If a seed is specified in the config, use it to seed our random number generator.
            // This ensures that using the same seed produces the exact same "random" flame.
            if (fractal_seed != 0) {
                rng.seed(fractal_seed);
            }

            if (settings.count("AnimationMode")) {
                std::string mode_str = settings.at("AnimationMode");
                std::transform(mode_str.begin(), mode_str.end(), mode_str.begin(), ::tolower);
                if (mode_str == "loop") animation_mode = LOOP;
                else if (mode_str == "random") animation_mode = RANDOM;
                else animation_mode = PING_PONG;
            }
        }

        for (const auto& pair : config_data) {
            const std::string& section_name = pair.first;
            if (section_name.rfind("State.", 0) == 0) {
                std::string temp = section_name.substr(6);
                size_t dot_pos = temp.find('.');
                if (dot_pos == std::string::npos) continue;

                int state_num = std::stoi(temp.substr(0, dot_pos));
                const auto& section = pair.second;

                Transform t;
                float a=0,b=0,c=0,d=0,e=0,f=0;
                // Use the new helper function to parse transform parameters
                if (section.count("a")) a = parse_float_or_random(section.at("a"), rng);
                if (section.count("b")) b = parse_float_or_random(section.at("b"), rng);
                if (section.count("c")) c = parse_float_or_random(section.at("c"), rng);
                if (section.count("d")) d = parse_float_or_random(section.at("d"), rng);
                if (section.count("e")) e = parse_float_or_random(section.at("e"), rng);
                if (section.count("f")) f = parse_float_or_random(section.at("f"), rng);
                t.params1 = glm::vec4(a,b,c,d);
                t.params2 = glm::vec4(e,f,0,0);

                if (section.count("color")) {
                    glm::vec3 color_vec;
                    std::stringstream ss(section.at("color"));
                    std::string component;
                    
                    // Parse each color component, which could be random
                    std::getline(ss, component, ',');
                    color_vec.r = parse_float_or_random(component, rng);
                    std::getline(ss, component, ',');
                    color_vec.g = parse_float_or_random(component, rng);
                    std::getline(ss, component);
                    color_vec.b = parse_float_or_random(component, rng);

                    t.color = glm::vec4(color_vec, 0.15f);
                }

                if (section.count("variation")) {
                    std::string var_str = section.at("variation");
                    trim(var_str);
                    if (var_str == "RANDOM") {
                        // Max enum value is HORSESHOE (4)
                        std::uniform_int_distribution<int> dist(0, 4); 
                        t.variation.x = static_cast<Variation>(dist(rng));
                    } else if (variation_map.count(var_str)) {
                        t.variation.x = variation_map.at(var_str);
                    }
                }
                state_map[state_num].push_back(t);
            }
        }
        
        if (!state_map.empty()) {
            int max_state = state_map.rbegin()->first;
            states.resize(max_state);
            for (const auto& pair : state_map) {
                if(pair.first > 0) states[pair.first - 1] = pair.second;
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "Error parsing config file: " << e.what() << std::endl;
        return false;
    }

    return true;
}
