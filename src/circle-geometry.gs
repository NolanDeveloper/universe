#version 440

layout (points) in;
layout (triangle_strip, max_vertices = 50) out;

in  vec3 PointColor[];
out vec3 FragColor;
out vec2 TextureCoord;

layout (location = 0) uniform float PointSize;

#define PI (4 * atan(1.))

void main() {
    FragColor = PointColor[0];

    vec2 offsets[4] = {
        vec2(-1, -1),
        vec2(-1, 1),
        vec2(1, 1),
        vec2(1, -1),
    };

    gl_Position = gl_in[0].gl_Position + vec4(-1, -1, 0.0, 0.0) * PointSize;
    TextureCoord = vec2(0, 0);
    EmitVertex();

    gl_Position = gl_in[0].gl_Position + vec4(-1,  1, 0.0, 0.0) * PointSize;
    TextureCoord = vec2(0, 1);
    EmitVertex();

    gl_Position = gl_in[0].gl_Position + vec4( 1, -1, 0.0, 0.0) * PointSize;
    TextureCoord = vec2(1, 0);
    EmitVertex();

    gl_Position = gl_in[0].gl_Position + vec4( 1,  1, 0.0, 0.0) * PointSize;
    TextureCoord = vec2(1, 1);
    EmitVertex();

    EndPrimitive();
}