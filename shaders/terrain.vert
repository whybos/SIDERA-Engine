#version 450

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 view;
    mat4 proj;
    mat4 viewProj;
    mat4 cascadeLightSpaceMatrices[4];
    vec4 cameraPos;
    vec4 sunDirection;     // xyz = direction TOWARDS the sun
    vec4 sunColor;         // rgb = color, a = intensity
    vec4 pointPosIntensity[2];
    vec4 pointColor[2];
    vec4 cascadeSplits;    // x, y, z, w = view-space depth split boundaries
    vec4 shadowParams;     // x = enabled, y = bias, z = pcfKernel, w = debugMode
    vec4 cascadeParams;    // x = cascadeCount, y = blendFraction, z = lambda, w = shadowDistance
    vec4 screenParams;     // x = width, y = height
} scene;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 terrainParams;    // x = width, y = length, z = heightScale, w = textureTiling
    vec4 viewportParams;   // x = viewportMode, y = normalSpace, z = nearPlane, w = farPlane
    vec4 lodParams;        // x = morphFactor, y = lodLevel, z = debugOverlayMode, w = stitchMask
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;   // x = parentWorldY (geomorphing), y = lodLevel, z = morphMask
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inNormal;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragColor;
layout(location = 3) out vec2 fragUV;
layout(location = 4) out vec2 fragTerrainUV;

void main() {
    // GPU Geomorphing: smoothly interpolate odd vertices toward parent surface height
    float morphFactor = clamp(pc.lodParams.x, 0.0, 1.0);
    vec3 morphedPos = inPosition;
    morphedPos.y = mix(inPosition.y, inColor.x, morphFactor * inColor.z);

    vec4 world = pc.model * vec4(morphedPos, 1.0);
    fragWorldPos = world.xyz;
    // Inverse-transpose normal transformation
    fragNormal = normalize(transpose(inverse(mat3(pc.model))) * inNormal);
    fragColor = inColor;
    fragUV = inUV;
    // Map world X and Z to [0, 1] normalized terrain UV coordinates for splat map sampling
    fragTerrainUV = vec2(world.x / pc.terrainParams.x + 0.5, world.z / pc.terrainParams.y + 0.5);
    gl_Position = scene.viewProj * world;
}
