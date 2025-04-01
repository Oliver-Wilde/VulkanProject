#version 450

// A simple vertex shader that expects a push constant containing a 4x4 matrix
// and a single attribute (location=0) for the vertex position.
//
// If you want to do bounding boxes, you can pass inPos as the 8 corners or more.

layout(location = 0) in vec3 inPos;

// We'll define a push constant block with a single transform (MVP or anything you like).
layout(push_constant) uniform PushConstantData
{
    mat4 transform;
} pc;

void main()
{
    // Multiply inPos by our transform to get clip-space position.
    gl_Position = pc.transform * vec4(inPos, 1.0);
}
