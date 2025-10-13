#version 430 core

struct Point {
    vec4 position;
    vec4 color;
};

layout(std430, binding = 1) readonly buffer PointBlock {
    Point points[];
};

out vec4 fragColor;
uniform mat4 projection;

void main() {
    Point p = points[gl_VertexID];
    gl_Position = projection * vec4(p.position.xy, 0.0, 1.0);
    fragColor = p.color;
}
