#version 450

layout(set = 0, binding = 0) uniform MVPBlock {
    mat4 mvp;
} ubo;

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inColor;
layout(location = 0) out vec3 fragColor;

void main()
{
    fragColor = inColor;
    gl_Position = ubo.mvp * vec4(inPos, 1.0);
}
