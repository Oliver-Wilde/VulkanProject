#version 450

layout(push_constant) uniform PCData
{
    vec3 center;    // (cx, cy, cz)
    vec3 scale;     // (sx, sy, sz)
} pc;

// If you have a uniform buffer for the MVP:
layout(set = 0, binding = 0) uniform MVPBlock
{
    mat4 mvp;
} ubo;

layout(location = 0) in vec3 inPos;  // Vertex position for the 1󪻑 cube

void main()
{
    // Expand the 1󪻑 cube to chunk size, then offset to chunk center
    vec3 worldPos = pc.center + pc.scale * inPos;

    gl_Position = ubo.mvp * vec4(worldPos, 1.0);
}
