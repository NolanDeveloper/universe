#version 440

in vec3 FragColor;
in vec2 TextureCoord;

out vec4 OutputColor;

layout (location = 0) uniform sampler2D Texture;

void main() {
    OutputColor = texture2D(Texture, TextureCoord) * vec4(FragColor, 1.0);
}