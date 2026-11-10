#version 450

// GPU-driven variant of triangle.vert with GPU Instancing (Milestone 16).
// Per-object transform and material come from the object SSBO, indexed via visibleIndices.
// gl_InstanceIndex == firstInstance (bInfo.firstInstance) + localInstanceIndex.
// visibleIndices[gl_InstanceIndex] contains the global index into objects[].
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
    vec4 windParams;            // x, y = windDir.xy (normalized), z = strength, w = speed
    vec4 timeParams;            // x = time (seconds), y = frequency, z = turbulence, w = windEnabled
} scene;

// Must match GpuObjectData in src/renderer/GpuDrivenScene.h / InstanceData.h (std430, 160 bytes).
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

layout(std430, set = 0, binding = 8) readonly buffer VisibleIndicesBuffer {
    uint visibleIndices[];
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
    uint objIndex = visibleIndices[gl_InstanceIndex];
    GpuObjectData obj = objects[objIndex];

    vec4 world = obj.model * vec4(inPosition, 1.0);

    // Foliage Wind Animation (Milestone 20)
    if ((obj.meshInfo.w & 1u) != 0u && scene.timeParams.w > 0.5) {
        float heightFactor = max(0.0, inPosition.y);
        float flex = inColor.r > 0.0 ? inColor.r : min(1.0, heightFactor * 0.5);
        float weight = clamp(heightFactor * flex, 0.0, 3.0);

        vec2 windDir = scene.windParams.xy;
        float windStrength = scene.windParams.z;
        float windSpeed = scene.windParams.w;
        float windFreq = scene.timeParams.y;
        float windTurb = scene.timeParams.z;
        float time = scene.timeParams.x;

        float wavePhase = (world.x * windDir.x + world.z * windDir.y) * windFreq + time * windSpeed;
        float flutter = sin(wavePhase * 2.5 + world.y * 3.0) * windTurb;
        float sway = (sin(wavePhase) + 0.35 * sin(wavePhase * 0.5) + flutter) * windStrength * weight;

        world.x += windDir.x * sway;
        world.z += windDir.y * sway;
        world.y -= abs(sway) * 0.15;
    }

    fragWorldPos = world.xyz;
    fragNormal = normalize(transpose(inverse(mat3(obj.model))) * inNormal);
    fragColor = inColor;
    fragUV = inUV;
    fragAlbedoMetallic = obj.albedoMetallic;
    fragRoughnessAoTex = obj.roughnessAoTex;
    gl_Position = scene.viewProj * world;
}
