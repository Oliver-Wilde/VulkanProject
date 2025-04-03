#version 450

// We discard color output so it never writes to color attachments
// (in a depth-only pass, there is no color attachment anyway)
void main()
{
    // No output
}
