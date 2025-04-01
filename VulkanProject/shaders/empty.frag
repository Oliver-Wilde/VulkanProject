#version 450

// This fragment shader does no color output.
// It's used in a depth-only occlusion pass.

void main()
{
    // No outputs => depth test only.
}
