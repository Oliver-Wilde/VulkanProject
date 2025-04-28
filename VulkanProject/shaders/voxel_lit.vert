#version 450
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;

layout(set = 0, binding = 0) uniform MVPBlock {
    mat4 mvp;
} ubo;

layout(push_constant) uniform PushConsts {
    vec3 lightDir;
    float _pad;
} pc;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec4 vColor;
layout(location = 2) flat out vec3 vLightDir;

void main()
{
    vWorldPos = inPos;
    vColor    = inColor;
    vLightDir = pc.lightDir;
    gl_Position = ubo.mvp * vec4(inPos, 1.0);
}
