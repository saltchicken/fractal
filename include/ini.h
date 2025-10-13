#pragma once
#include <string>
#include <map>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace simpleini {

// Helper function to trim whitespace from both ends of a string
static inline void trim(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), s.end());
}


class INIReader {
public:
    using Section = std::map<std::string, std::string>;
    using Sections = std::map<std::string, Section>;

    // Loads and parses the INI file. Returns false if the file cannot be opened.
    bool load(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            return false;
        }

        std::string line;
        std::string currentSection;

        while (std::getline(file, line)) {
            trim(line);
            if (line.empty() || line[0] == ';' || line[0] == '#') {
                continue; // Skip comments and empty lines
            }

            if (line[0] == '[' && line.back() == ']') {
                currentSection = line.substr(1, line.length() - 2);
                trim(currentSection);
            } else {
                size_t equalsPos = line.find('=');
                if (equalsPos != std::string::npos) {
                    std::string key = line.substr(0, equalsPos);
                    std::string value = line.substr(equalsPos + 1);
                    trim(key);
                    trim(value);
                    if (!currentSection.empty()) {
                        data[currentSection][key] = value;
                    }
                }
            }
        }
        return true;
    }

    // Provides access to the parsed data
    const Sections& get_data() const {
        return data;
    }

private:
    Sections data;
};

} // namespace simpleini
