#version 450

layout(set = 0, binding = 0) uniform MVPBlock {
    mat4 mvp;
} ubo;

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;   // ← packed RGBA8 arrives as vec4

layout(location = 0) out vec3 fragColor;

void main()
{
    fragColor = inColor.rgb;             // ignore alpha (or keep it)
    gl_Position = ubo.mvp * vec4(inPos, 1.0);
}
