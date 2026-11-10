#version 450

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 view;
    mat4 proj;
    mat4 viewProj;
    mat4 cascadeLightSpaceMatrices[4];
    vec4 cameraPos;
    vec4 sunDirection;     // xyz = direction TOWARDS the sun
    vec4 sunColor;         // rgb = color, a = intensity
    vec4 pointPosIntensity[2];  // xyz = world position, w = intensity
    vec4 pointColor[2];         // rgb = color
    vec4 cascadeSplits;         // x, y, z, w = view-space depth split boundaries
    vec4 shadowParams;          // x = enabled, y = bias, z = pcfKernel, w = debugMode
    vec4 cascadeParams;         // x = cascadeCount, y = blendFraction, z = lambda, w = shadowDistance
    vec4 screenParams;          // x = width, y = height
    vec4 windParams;            // x, y = windDir.xy (normalized), z = strength, w = speed
    vec4 timeParams;            // x = time (seconds), y = frequency, z = turbulence, w = windEnabled
} scene;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 albedoMetallic;
    vec4 roughnessAoTex;
    vec4 viewportParams; // x = viewportMode, y = normalSpace, z = nearPlane, w = farPlane
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inNormal;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragColor;
layout(location = 3) out vec2 fragUV;
layout(location = 4) flat out vec4 fragAlbedoMetallic;   // rgb = albedo tint, a = metallic
layout(location = 5) flat out vec4 fragRoughnessAoTex;   // x = roughness, y = ao, z = texture strength

void main() {
    vec4 world = pc.model * vec4(inPosition, 1.0);

    // Foliage Wind Animation (Milestone 20)
    if (scene.timeParams.w > 0.5 && pc.roughnessAoTex.w > 0.0) {
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
    // Inverse-transpose keeps normals correct under non-uniform scale
    fragNormal = normalize(transpose(inverse(mat3(pc.model))) * inNormal);
    fragColor = inColor;
    fragUV = inUV;
    fragAlbedoMetallic = pc.albedoMetallic;
    fragRoughnessAoTex = pc.roughnessAoTex;
    gl_Position = scene.viewProj * world;
}
