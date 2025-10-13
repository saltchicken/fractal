#version 330 core

out vec4 FragColor;

uniform float u_persistence;

void main() {
    FragColor = vec4(0.0, 0.0, 0.0, 1.0 - u_persistence);
}
