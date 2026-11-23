#version 450

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;
layout(location = 3) in float inUseFont;

layout(push_constant) uniform UIPushConstants {
    vec2 invViewport;
} pc;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 fragColor;
layout(location = 2) out float fragUseFont;

void main() {
    vec2 ndc = inPos * pc.invViewport * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
    fragUV = inUV;
    fragColor = inColor;
    fragUseFont = inUseFont;
}
