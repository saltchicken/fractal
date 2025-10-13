#!/bin/bash
# Exit immediately if a command exits with a non-zero status.
set -e

# Define the build directory name
BUILD_DIR="build"

# Clean and create the build directory
echo "--- Cleaning and preparing build directory ---"
rm -rf "$BUILD_DIR"
mkdir "$BUILD_DIR"

# Configure the project with CMake
echo "--- Configuring with CMake ---"
cmake -S . -B "$BUILD_DIR"

# Build the project
echo "--- Building project ---"
cmake --build "$BUILD_DIR"

echo "--- Build complete! ---"
