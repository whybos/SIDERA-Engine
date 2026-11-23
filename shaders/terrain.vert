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
    vec4 edgeMorph;        // x = northMorph, y = southMorph, z = westMorph, w = eastMorph
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;   // x = parentWorldY, y = lodLevel, z = edgeRole (0=even,1=int,2=N,3=S,4=W,5=E)
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inNormal;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragColor;
layout(location = 3) out vec2 fragUV;
layout(location = 4) out vec2 fragTerrainUV;

void main() {
    // Neighbor-synchronized GPU Geomorphing (Milestone 23.1)
    float role = inColor.z;
    float m = 0.0;
    if (role > 0.5) {
        if (role < 1.5)      m = pc.lodParams.x; // interior odd
        else if (role < 2.5) m = pc.edgeMorph.x; // North edge odd
        else if (role < 3.5) m = pc.edgeMorph.y; // South edge odd
        else if (role < 4.5) m = pc.edgeMorph.z; // West edge odd
        else                 m = pc.edgeMorph.w; // East edge odd
    }
    m = clamp(m, 0.0, 1.0);

    vec3 morphedPos = inPosition;
    morphedPos.y = mix(inPosition.y, inColor.x, m);

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
