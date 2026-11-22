#version 450
// Samples the draw's texture (nearest, like the CPU rasteriser). Alpha blending is
// configured in the pipeline, so opaque texels (alpha=255) overwrite and translucent
// ones blend, reproducing the software compositor.
layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D tex;
void main() {
    outColor = texture(tex, fragUV);
}
