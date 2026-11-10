#version 450

// GPU-driven variant of triangle.vert (Milestone 14).
// Per-object transform and material come from the object SSBO, indexed by gl_InstanceIndex.
// gl_InstanceIndex == firstInstance (written by gen_draw_commands.comp) + instance (always 0).
// Requires the drawIndirectFirstInstance device feature.

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
};

layout(std430, set = 0, binding = 6) readonly buffer ObjectBuffer {
    GpuObjectData objects[];
};

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inNormal;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragColor;
layout(location = 3) out vec2 fragUV;
layout(location = 4) flat out vec4 fragAlbedoMetallic;
layout(location = 5) flat out vec4 fragRoughnessAoTex;

void main() {
    GpuObjectData obj = objects[gl_InstanceIndex];

    vec4 world = obj.model * vec4(inPosition, 1.0);
    fragWorldPos = world.xyz;
    // Same normal transform as the CPU-driven path (inverse-transpose of the model's 3x3)
    fragNormal = normalize(transpose(inverse(mat3(obj.model))) * inNormal);
    fragColor = inColor;
    fragUV = inUV;
    fragAlbedoMetallic = obj.albedoMetallic;
    fragRoughnessAoTex = obj.roughnessAoTex;
    gl_Position = scene.viewProj * world;
}
