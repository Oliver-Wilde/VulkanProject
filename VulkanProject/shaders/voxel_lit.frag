#version 450
// voxel_lit.frag – brighter ambient

layout(location = 0) in  vec3 vWorldPos;
layout(location = 1) in  vec4 vColor;
layout(location = 2) flat in vec3 vLightDir;

layout(location = 0) out vec4 outColor;

void main()
{
    vec3  n   = normalize(cross(dFdx(vWorldPos), dFdy(vWorldPos)));

    // brighter ambient term
    const float AMBIENT = 0.35;               // was 0.20
    float diff = max(dot(n, -vLightDir), 0.0);
    float lit  = AMBIENT + (1.0 - AMBIENT) * diff;

    outColor = vec4(vColor.rgb * lit, vColor.a);
}
