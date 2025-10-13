#pragma once

#include <string>
#include <glad/glad.h>

// Creates a graphics shader program from vertex and fragment shader files
GLuint create_shader_program_from_files(const std::string& vs_path, const std::string& fs_path);

// Creates a compute shader program from a compute shader file
GLuint create_compute_shader_program_from_file(const std::string& cs_path);
