#version 450
// Textured-quad vertex shader for GPU rasterisation of the GX2 command stream.
// Input positions are in framebuffer pixel space [0,W]x[0,H]; Vulkan clip-space y
// points down, matching pixel-y-down, so no flip is needed.
layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 0) out vec2 fragUV;
layout(push_constant) uniform PushConstants { vec2 invSize; } pc;
void main() {
    vec2 ndc = inPos * pc.invSize * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
    fragUV = inUV;
}
