#version 330 core
out vec4 FragColor;
in vec2 TexCoords;

uniform sampler2D screenTexture;

void main() {
    vec3 color = texture(screenTexture, TexCoords).rgb;
    // Simple gamma correction
    color = pow(color, vec3(0.8));
    FragColor = vec4(color, 1.0);
}
