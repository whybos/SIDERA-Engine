#version 450

layout(set = 0, binding = 1) uniform sampler2D uAlbedoMap;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 texSample = texture(uAlbedoMap, fragUV);
    outColor = vec4(fragColor * texSample.rgb, texSample.a);
}
