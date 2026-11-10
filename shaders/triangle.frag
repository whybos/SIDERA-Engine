#version 450

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
    vec4 cascadeSplits;         // x, y, z, w = view-space depth split boundaries (near to far)
    vec4 shadowParams;          // x = enabled, y = bias, z = pcfKernel, w = debugMode
    vec4 cascadeParams;         // x = cascadeCount, y = blendFraction, z = lambda, w = shadowDistance
    vec4 screenParams;          // x = width, y = height
} scene;

layout(set = 0, binding = 1) uniform sampler2D uAlbedoMap;
layout(set = 0, binding = 2) uniform samplerCube uIrradianceMap;
layout(set = 0, binding = 3) uniform samplerCube uPrefilteredMap;
layout(set = 0, binding = 4) uniform sampler2D uBrdfLut;
layout(set = 0, binding = 5) uniform sampler2DArray uShadowMap;

// Per-object material data. Supplied by the vertex stage as flat varyings so the same fragment
// shader serves both the CPU-driven path (push constants) and the GPU-driven path (object SSBO).
layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragColor;
layout(location = 3) in vec2 fragUV;
layout(location = 4) flat in vec4 fragAlbedoMetallic;   // rgb = albedo tint, a = metallic
layout(location = 5) flat in vec4 fragRoughnessAoTex;   // x = roughness, y = ao, z = texture strength

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265358979323846;
const float MAX_REFLECTION_LOD = 4.0; // 5 mip levels: 0, 1, 2, 3, 4

// Trowbridge-Reitz GGX normal distribution
float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

// Schlick-GGX geometry term
float geometrySchlickGGX(float NdotX, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotX / (NdotX * (1.0 - k) + k);
}

// Smith geometry (shadowing * masking)
float geometrySmith(float NdotV, float NdotL, float roughness) {
    return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Cook-Torrance radiance contribution of a direct light
vec3 evaluateLight(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo, float metallic, float roughness, vec3 F0) {
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0001);

    float D = distributionGGX(N, H, roughness);
    float G = geometrySmith(NdotV, NdotL, roughness);
    vec3  F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 specular = (D * G * F) / (4.0 * NdotV * NdotL + 0.0001);

    // Energy conservation
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    return (kD * albedo / PI + specular) * radiance * NdotL;
}

// Directional cascade shadow sampling with 3x3 PCF and slope-scaled depth bias
float sampleCascadeShadow(vec3 worldPos, vec3 N, vec3 L, int layer) {
    vec4 fragPosLightSpace = scene.cascadeLightSpaceMatrices[layer] * vec4(worldPos, 1.0);
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;

    // Fragments outside the cascade's orthographic projection are not shadowed
    if (projCoords.z > 1.0 || projCoords.z < 0.0 ||
        projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 0.0;
    }

    // Slope-scaled depth bias, scaled proportionally for further cascades
    float cosTheta = max(dot(N, L), 0.0);
    float cascadeBiasScale = 1.0 + float(layer) * 0.45;
    float bias = max(scene.shadowParams.y * (1.0 - cosTheta), scene.shadowParams.y * 0.20) * cascadeBiasScale;

    float currentDepth = projCoords.z;
    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(uShadowMap, 0).xy);

    // 3x3 Percentage Closer Filtering (PCF) on the 2D array layer
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            vec2 uv = projCoords.xy + vec2(x, y) * texelSize;
            float pcfDepth = texture(uShadowMap, vec3(uv, float(layer))).r;
            shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
}

// Cascaded Shadow Map calculation with dynamic split evaluation and smooth cascade transition blending
float calculateCascadeShadow(vec3 worldPos, vec3 N, vec3 L, out int outCascadeIndex, out float outBlendFactor) {
    outCascadeIndex = 0;
    outBlendFactor = 0.0;

    if (scene.shadowParams.x < 0.5) {
        return 0.0; // Shadows disabled
    }

    vec4 viewPos = scene.view * vec4(worldPos, 1.0);
    float depthValue = abs(viewPos.z);

    int cascadeIndex = 0;
    int numCascades = clamp(int(scene.cascadeParams.x), 1, 4);
    for (int i = 0; i < numCascades - 1; ++i) {
        if (depthValue > scene.cascadeSplits[i]) {
            cascadeIndex = i + 1;
        }
    }
    outCascadeIndex = cascadeIndex;

    float shadow = sampleCascadeShadow(worldPos, N, L, cascadeIndex);

    // Smooth transition blending near cascade boundaries
    float blendFraction = scene.cascadeParams.y; // e.g. 0.15
    if (cascadeIndex < numCascades - 1 && blendFraction > 0.001) {
        float splitEnd = scene.cascadeSplits[cascadeIndex];
        float splitStart = (cascadeIndex == 0) ? 0.0 : scene.cascadeSplits[cascadeIndex - 1];
        float splitRange = splitEnd - splitStart;
        float blendThreshold = splitEnd - splitRange * blendFraction;

        if (depthValue > blendThreshold) {
            float blendFactor = clamp((depthValue - blendThreshold) / max(splitEnd - blendThreshold, 0.0001), 0.0, 1.0);
            outBlendFactor = blendFactor;
            float nextShadow = sampleCascadeShadow(worldPos, N, L, cascadeIndex + 1);
            shadow = mix(shadow, nextShadow, blendFactor);
        }
    }

    return shadow;
}

void main() {
    float metallic  = clamp(fragAlbedoMetallic.a, 0.0, 1.0);
    float roughness = clamp(fragRoughnessAoTex.x, 0.04, 1.0);
    float ao        = clamp(fragRoughnessAoTex.y, 0.0, 1.0);

    vec3 texAlbedo = mix(vec3(1.0), texture(uAlbedoMap, fragUV).rgb, fragRoughnessAoTex.z);
    vec3 albedo = fragAlbedoMetallic.rgb * fragColor * texAlbedo;

    vec3 N = normalize(fragNormal);
    vec3 V = normalize(scene.cameraPos.xyz - fragWorldPos);
    vec3 R = reflect(-V, N);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // -----------------------------------------------------------------
    // 1. Direct Lighting (Directional Sun with CSM + Point Lights)
    // -----------------------------------------------------------------
    vec3 Lo = vec3(0.0);

    // Directional sun: calculate cascade shadow visibility and attenuate ONLY direct sunlight
    vec3 Ls = normalize(scene.sunDirection.xyz);
    int cascadeIndex = 0;
    float blendFactor = 0.0;
    float shadow = calculateCascadeShadow(fragWorldPos, N, Ls, cascadeIndex, blendFactor);
    float shadowFactor = 1.0 - shadow;
    vec3 sunDirect = evaluateLight(N, V, Ls, scene.sunColor.rgb * scene.sunColor.a, albedo, metallic, roughness, F0);
    Lo += sunDirect * shadowFactor;

    // Point lights with inverse-square falloff (unaffected by directional sunlight shadow)
    for (int i = 0; i < 2; ++i) {
        vec3 toLight = scene.pointPosIntensity[i].xyz - fragWorldPos;
        float dist2 = max(dot(toLight, toLight), 0.01);
        vec3 L = toLight * inversesqrt(dist2);
        vec3 radiance = scene.pointColor[i].rgb * scene.pointPosIntensity[i].w / dist2;
        Lo += evaluateLight(N, V, L, radiance, albedo, metallic, roughness, F0);
    }

    // -----------------------------------------------------------------
    // 2. Image-Based Lighting (IBL)
    // -----------------------------------------------------------------
    float NdotV = max(dot(N, V), 0.0);
    vec3 F = fresnelSchlickRoughness(NdotV, F0, roughness);

    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

    // Diffuse IBL: Irradiance convolution map
    vec3 irradiance = texture(uIrradianceMap, N).rgb;
    vec3 diffuseIBL = irradiance * albedo;

    // Specular IBL: Prefiltered environment cubemap sampled at LOD = roughness * MAX_LOD
    vec3 prefilteredColor = textureLod(uPrefilteredMap, R, roughness * MAX_REFLECTION_LOD).rgb;
    // BRDF integration LUT: x = scale (F0), y = bias
    vec2 brdf = texture(uBrdfLut, vec2(NdotV, roughness)).rg;
    vec3 specularIBL = prefilteredColor * (F * brdf.x + brdf.y);

    vec3 ambient = (kD * diffuseIBL + specularIBL) * ao;

    // Total radiance
    vec3 color = Lo + ambient;

    // Reinhard tone mapping
    color = color / (color + vec3(1.0));

    // -----------------------------------------------------------------
    // 3. Debug Visualization Modes (F7 / CLI)
    // -----------------------------------------------------------------
    int debugMode = int(scene.shadowParams.w + 0.5);

    if (debugMode == 1) {
        // Mode 1: Cascade Split Color Overlay with smooth transition visualization
        const vec3 cascadeColors[4] = vec3[4](
            vec3(1.0, 0.22, 0.22),  // Cascade 0: Red
            vec3(0.22, 1.0, 0.22),  // Cascade 1: Green
            vec3(0.25, 0.55, 1.0),  // Cascade 2: Blue
            vec3(1.0, 0.90, 0.20)   // Cascade 3: Yellow
        );
        vec3 tint = cascadeColors[clamp(cascadeIndex, 0, 3)];
        if (blendFactor > 0.0 && cascadeIndex < 3) {
            tint = mix(tint, cascadeColors[cascadeIndex + 1], blendFactor);
        }
        color = mix(color, tint, 0.45);
    } else if (debugMode == 2) {
        // Mode 2: Picture-in-Picture thumbnail in top-right corner (Cascade 0 depth)
        vec2 screenCoord = gl_FragCoord.xy;
        vec2 pipSize = vec2(360.0, 360.0);
        vec2 pipPos = vec2(scene.screenParams.x - pipSize.x - 24.0, 24.0);
        if (screenCoord.x >= pipPos.x && screenCoord.x <= pipPos.x + pipSize.x &&
            screenCoord.y >= pipPos.y && screenCoord.y <= pipPos.y + pipSize.y) {
            // Gold border
            if (screenCoord.x <= pipPos.x + 2.0 || screenCoord.x >= pipPos.x + pipSize.x - 2.0 ||
                screenCoord.y <= pipPos.y + 2.0 || screenCoord.y >= pipPos.y + pipSize.y - 2.0) {
                outColor = vec4(1.0, 0.78, 0.1, 1.0);
                return;
            }
            vec2 pipUV = (screenCoord - pipPos) / pipSize;
            float d = texture(uShadowMap, vec3(pipUV, 0.0)).r;
            outColor = vec4(vec3(d), 1.0);
            return;
        }
    } else if (debugMode == 3) {
        // Mode 3: Fullscreen shadow depth map view (Cascade 0)
        vec2 screenUV = gl_FragCoord.xy / scene.screenParams.xy;
        float d = texture(uShadowMap, vec3(screenUV, 0.0)).r;
        outColor = vec4(vec3(d), 1.0);
        return;
    } else if (debugMode == 4) {
        // Mode 4: Shadow factor mask (white = fully lit, black = shadowed)
        outColor = vec4(vec3(shadowFactor), 1.0);
        return;
    }

    outColor = vec4(color, 1.0);
}
