#version 450

layout(set = 0, binding = 0) uniform sampler2D uFontAtlas;

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragColor;
layout(location = 2) in float fragUseFont;

layout(location = 0) out vec4 outColor;

void main() {
    if (fragUseFont > 0.5) {
        float cov = texture(uFontAtlas, fragUV).a;
        if (cov < 0.05) discard;
        outColor = vec4(fragColor.rgb, fragColor.a * cov);
    } else {
        outColor = fragColor;
    }
}
