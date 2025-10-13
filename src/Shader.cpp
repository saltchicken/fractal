#include "Shader.h"
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>

// --- Helper function to load a file's content into a string ---
std::string load_shader_source(const std::string& filepath) {
    std::ifstream shader_file(filepath);
    if (!shader_file.is_open()) {
        std::cerr << "ERROR: Could not open shader file: " << filepath << std::endl;
        return "";
    }
    std::stringstream buffer;
    buffer << shader_file.rdbuf();
    return buffer.str();
}

// --- Helper function for checking shader compilation/linking errors ---
void check_compile_errors(GLuint shader, std::string type) {
    GLint success;
    GLchar infoLog[1024];
    if (type != "PROGRAM") {
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(shader, 1024, NULL, infoLog);
            std::cerr << "ERROR::SHADER_COMPILATION_ERROR of type: " << type << "\n" << infoLog << "\n -- --------------------------------------------------- -- " << std::endl;
        }
    } else {
        glGetProgramiv(shader, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(shader, 1024, NULL, infoLog);
            std::cerr << "ERROR::PROGRAM_LINKING_ERROR of type: " << type << "\n" << infoLog << "\n -- --------------------------------------------------- -- " << std::endl;
        }
    }
}


GLuint create_shader_program_from_files(const std::string& vs_path, const std::string& fs_path) {
    std::string vertex_code_str = load_shader_source(vs_path);
    std::string fragment_code_str = load_shader_source(fs_path);

    const char* v_shader_code = vertex_code_str.c_str();
    const char* f_shader_code = fragment_code_str.c_str();

    // 2. compile shaders
    unsigned int vertex, fragment;
    
    // vertex shader
    vertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex, 1, &v_shader_code, NULL);
    glCompileShader(vertex);
    check_compile_errors(vertex, "VERTEX");
    
    // fragment Shader
    fragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment, 1, &f_shader_code, NULL);
    glCompileShader(fragment);
    check_compile_errors(fragment, "FRAGMENT");
    
    // shader Program
    GLuint id = glCreateProgram();
    glAttachShader(id, vertex);
    glAttachShader(id, fragment);
    glLinkProgram(id);
    check_compile_errors(id, "PROGRAM");
    
    // delete the shaders as they're linked into our program now and no longer necessary
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    return id;
}


GLuint create_compute_shader_program_from_file(const std::string& cs_path) {
    std::string compute_code_str = load_shader_source(cs_path);
    const char* c_shader_code = compute_code_str.c_str();

    // compile shader
    unsigned int compute;
    compute = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(compute, 1, &c_shader_code, NULL);
    glCompileShader(compute);
    check_compile_errors(compute, "COMPUTE");

    // shader program
    GLuint id = glCreateProgram();
    glAttachShader(id, compute);
    glLinkProgram(id);
    check_compile_errors(id, "PROGRAM");

    glDeleteShader(compute);

    return id;
}
