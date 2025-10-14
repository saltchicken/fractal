#include "Shader.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <gtc/type_ptr.hpp>

namespace {
    // Helper function to load a shader's source code from a file
    std::string loadShaderSource(const std::string& filepath) {
        std::ifstream shaderFile(filepath);
        if (!shaderFile.is_open()) {
            std::cerr << "ERROR: Could not open shader file: " << filepath << std::endl;
            // Throwing an exception might be better in a real-world scenario
            return "";
        }
        std::stringstream buffer;
        buffer << shaderFile.rdbuf();
        return buffer.str();
    }
}

Shader::Shader(const std::string& vertexPath, const std::string& fragmentPath) {
    std::string vertexCode = loadShaderSource(vertexPath);
    std::string fragmentCode = loadShaderSource(fragmentPath);
    const char* vShaderCode = vertexCode.c_str();
    const char* fShaderCode = fragmentCode.c_str();

    GLuint vertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex, 1, &vShaderCode, nullptr);
    glCompileShader(vertex);
    checkCompileErrors(vertex, "VERTEX");

    GLuint fragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment, 1, &fShaderCode, nullptr);
    glCompileShader(fragment);
    checkCompileErrors(fragment, "FRAGMENT");

    m_id = glCreateProgram();
    glAttachShader(m_id, vertex);
    glAttachShader(m_id, fragment);
    glLinkProgram(m_id);
    checkCompileErrors(m_id, "PROGRAM");

    glDeleteShader(vertex);
    glDeleteShader(fragment);
}

Shader::Shader(const std::string& computePath) {
    std::string computeCode = loadShaderSource(computePath);
    const char* cShaderCode = computeCode.c_str();

    GLuint compute = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(compute, 1, &cShaderCode, nullptr);
    glCompileShader(compute);
    checkCompileErrors(compute, "COMPUTE");

    m_id = glCreateProgram();
    glAttachShader(m_id, compute);
    glLinkProgram(m_id);
    checkCompileErrors(m_id, "PROGRAM");

    glDeleteShader(compute);
}

Shader::~Shader() {
    if (m_id != 0) {
        glDeleteProgram(m_id);
    }
}

Shader::Shader(Shader&& other) noexcept : m_id(other.m_id), m_uniform_location_cache(std::move(other.m_uniform_location_cache)) {
    other.m_id = 0;
}

Shader& Shader::operator=(Shader&& other) noexcept {
    if (this != &other) {
        if (m_id != 0) {
            glDeleteProgram(m_id);
        }
        m_id = other.m_id;
        m_uniform_location_cache = std::move(other.m_uniform_location_cache);
        other.m_id = 0;
    }
    return *this;
}

void Shader::use() const {
    glUseProgram(m_id);
}

void Shader::setInt(const std::string& name, int value) {
    glUniform1i(getUniformLocation(name), value);
}

void Shader::setUint(const std::string& name, unsigned int value) {
    glUniform1ui(getUniformLocation(name), value);
}

void Shader::setFloat(const std::string& name, float value) {
    glUniform1f(getUniformLocation(name), value);
}

void Shader::setVec2(const std::string& name, const glm::vec2& value) {
    glUniform2fv(getUniformLocation(name), 1, &value[0]);
}

void Shader::setMat4(const std::string& name, const glm::mat4& mat) {
    glUniformMatrix4fv(getUniformLocation(name), 1, GL_FALSE, &mat[0][0]);
}

GLint Shader::getUniformLocation(const std::string& name) const {
    if (m_uniform_location_cache.count(name)) {
        return m_uniform_location_cache.at(name);
    }
    GLint location = glGetUniformLocation(m_id, name.c_str());
    m_uniform_location_cache[name] = location;
    return location;
}

void Shader::checkCompileErrors(GLuint shader, const std::string& type) {
    GLint success;
    GLchar infoLog[1024];
    if (type != "PROGRAM") {
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(shader, 1024, nullptr, infoLog);
            std::cerr << "ERROR::SHADER_COMPILATION_ERROR of type: " << type << "\n" << infoLog << "\n -- --------------------------------------------------- -- " << std::endl;
        }
    } else {
        glGetProgramiv(shader, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(shader, 1024, nullptr, infoLog);
            std::cerr << "ERROR::PROGRAM_LINKING_ERROR of type: " << type << "\n" << infoLog << "\n -- --------------------------------------------------- -- " << std::endl;
        }
    }
}
