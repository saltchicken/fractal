# CMake minimum version required
cmake_minimum_required(VERSION 3.12)
# Define the project
project(FractalFlame VERSION 1.0)
# Set the C++ standard to C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Find Dependencies ---
# Find OpenGL
find_package(OpenGL REQUIRED)
# Find GLFW3. You may need to install this via your system's package manager
find_package(glfw3 REQUIRED)

# --- Define Source Files ---
# List all source files for our executable
set(SOURCE_FILES
    src/main.cpp
    src/Application.cpp # Add the new source file
    src/Shader.cpp
    libs/glad/src/glad.c
)

# --- Create Executable ---
# Add the executable target
add_executable(FractalFlame ${SOURCE_FILES})

# --- Link Libraries and Include Directories ---
# Add include directories for our libraries
target_include_directories(FractalFlame PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/glad/include
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/glm
    ${OPENGL_INCLUDE_DIR}
    ${glfw3_INCLUDE_DIRS}
)

# Link the required libraries to our executable
target_link_libraries(FractalFlame PRIVATE
    ${OPENGL_LIBRARIES}
    glfw
)

# Print a message upon configuration
message(STATUS "Project configured. Build with 'cmake --build build'")
