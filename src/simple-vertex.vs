#version 440

layout (location = 0) in vec2 Position;
layout (location = 1) in vec3 Color;

out vec3 PointColor;

void main() {
    gl_Position = vec4(Position, 0., 1.);
    PointColor = Color;
}