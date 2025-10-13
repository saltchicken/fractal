#include "Config.h"
#include "ini.h"
#include <iostream>
#include <map>
#include <sstream>
#include <algorithm>

// Map to convert string variations from the config file to the Variation enum
const std::map<std::string, Variation> variation_map = {
    {"LINEAR", LINEAR}, {"SINUSOIDAL", SINUSOIDAL}, {"SPHERICAL", SPHERICAL},
    {"SWIRL", SWIRL}, {"HORSESHOE", HORSESHOE}
};

bool Config::load(const std::string& filename) {
    states.clear();
    simpleini::INIReader reader;
    if (!reader.load(filename)) {
        std::cerr << "Failed to load " << filename << std::endl;
        return false;
    }

    const auto& config_data = reader.get_data();
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
