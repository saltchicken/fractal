#include "Config.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <map>
#include <algorithm>
#include <cctype>

// Anonymous namespace to hold the helper function, keeping it private to this file.
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

bool Config::load(const std::string& filename) {
    states.clear();

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
                if (section.count("a")) a = std::stof(section.at("a"));
                if (section.count("b")) b = std::stof(section.at("b"));
                if (section.count("c")) c = std::stof(section.at("c"));
                if (section.count("d")) d = std::stof(section.at("d"));
                if (section.count("e")) e = std::stof(section.at("e"));
                if (section.count("f")) f = std::stof(section.at("f"));
                t.params1 = glm::vec4(a,b,c,d);
                t.params2 = glm::vec4(e,f,0,0);
                if (section.count("color")) {
                    glm::vec3 color_vec;
                    std::stringstream ss(section.at("color"));
                    ss >> color_vec.r; ss.ignore(); ss >> color_vec.g; ss.ignore(); ss >> color_vec.b;
                    t.color = glm::vec4(color_vec, 0.15f);
                }
                if (section.count("variation")) {
                    std::string var_str = section.at("variation");
                    if (variation_map.count(var_str)) { t.variation.x = variation_map.at(var_str); }
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
