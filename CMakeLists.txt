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
# (e.g., `sudo apt-get install libglfw3-dev` or `brew install glfw`)
find_package(glfw3 REQUIRED)

# --- Define Source Files ---

# Gather all the necessary source files for Dear ImGui
file(GLOB IMGUI_SOURCES
    "libs/imgui/imgui.cpp"
    "libs/imgui/imgui_draw.cpp"
    "libs/imgui/imgui_tables.cpp"
    "libs/imgui/imgui_widgets.cpp"
    "libs/imgui/backends/imgui_impl_glfw.cpp"
    "libs/imgui/backends/imgui_impl_opengl3.cpp"
)

# List all source files for our executable
set(SOURCE_FILES
    main.cpp
    libs/glad/src/glad.c
    ${IMGUI_SOURCES}
)

# --- Create Executable ---

# Add the executable target
add_executable(FractalFlame ${SOURCE_FILES})

# --- Link Libraries and Include Directories ---

# Add include directories for our libraries
target_include_directories(FractalFlame PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/glad/include
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/imgui/backends
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/glm
    ${OPENGL_INCLUDE_DIR}
    ${glfw3_INCLUDE_DIRS}
)

# Link the required libraries to our executable
target_link_libraries(FractalFlame PRIVATE
    ${OPENGL_LIBRARIES}
    glfw
)

# --- Platform Specific Settings ---
if (APPLE)
    # Required for modern OpenGL on macOS
    target_link_libraries(FractalFlame PRIVATE "-framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo")
endif()

# Print a message upon configuration
message(STATUS "Project configured. Build with 'cmake --build build'")
