#pragma once

#include <string>
#include <unordered_map>
#include <glad/glad.h>
#include <glm.hpp>

// A RAII-compliant shader program class
class Shader {
public:
    // Constructor for graphics shaders (vertex + fragment)
    Shader(const std::string& vertexPath, const std::string& fragmentPath);
    // Constructor for compute shaders
    explicit Shader(const std::string& computePath);

    ~Shader();

    // Disable copying
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    // Enable moving
    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;

    // Use/activate the shader
    void use() const;

    // Utility uniform functions
    void setInt(const std::string& name, int value);
    void setUint(const std::string& name, unsigned int value);
    void setFloat(const std::string& name, float value);
    void setVec2(const std::string& name, const glm::vec2& value);
    void setMat4(const std::string& name, const glm::mat4& value);

private:
    GLuint m_id = 0;
    // For caching uniform locations to avoid repeated lookups
    mutable std::unordered_map<std::string, GLint> m_uniform_location_cache;

    GLint getUniformLocation(const std::string& name) const;
    void checkCompileErrors(GLuint shader, const std::string& type);
};
