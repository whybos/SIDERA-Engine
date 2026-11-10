#version 450

// GPU-driven variant of shadow_depth.vert (Milestone 14).
// Cascade light-space matrices come from the scene UBO; model transforms from the object SSBO.

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 view;
    mat4 proj;
    mat4 viewProj;
    mat4 cascadeLightSpaceMatrices[4];
    vec4 cameraPos;
    vec4 sunDirection;
    vec4 sunColor;
    vec4 pointPosIntensity[2];
    vec4 pointColor[2];
    vec4 cascadeSplits;
    vec4 shadowParams;
    vec4 cascadeParams;
    vec4 screenParams;
} scene;

// Must match GpuObjectData in src/renderer/GpuDrivenScene.h (std430, 112 bytes).
struct GpuObjectData {
    mat4 model;
    vec4 albedoMetallic;
    vec4 roughnessAoTex;
    uvec4 meshInfo;
    vec4 boundingSphere;
    vec4 aabbMin;
    vec4 aabbMax;
};

layout(std430, set = 0, binding = 6) readonly buffer ObjectBuffer {
    GpuObjectData objects[];
};

layout(push_constant) uniform ShadowIndirectPushConstants {
    uint cascadeIndex;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inNormal;

void main() {
    gl_Position = scene.cascadeLightSpaceMatrices[pc.cascadeIndex]
                * objects[gl_InstanceIndex].model
                * vec4(inPosition, 1.0);
}
