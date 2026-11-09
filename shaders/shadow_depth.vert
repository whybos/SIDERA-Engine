#version 450

layout(push_constant) uniform ShadowPushConstants {
    mat4 lightSpaceMatrix;
    mat4 model;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inNormal;

void main() {
    gl_Position = pc.lightSpaceMatrix * pc.model * vec4(inPosition, 1.0);
}
