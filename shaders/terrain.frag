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
    vec4 cascadeSplits;    // x, y, z, w = view-space depth split boundaries (near to far)
    vec4 shadowParams;     // x = enabled, y = bias, z = pcfKernel, w = debugMode
    vec4 cascadeParams;    // x = cascadeCount, y = blendFraction, z = lambda, w = shadowDistance
    vec4 screenParams;     // x = width, y = height
} scene;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 terrainParams;    // x = width, y = length, z = heightScale, w = textureTiling
    vec4 viewportParams;   // x = viewportMode, y = normalSpace (0=world, 1=view), z = nearPlane, w = farPlane
} pc;

// Universal Scene Environment & Shadow Bindings (Set 0)
layout(set = 0, binding = 2) uniform samplerCube uIrradianceMap;
layout(set = 0, binding = 3) uniform samplerCube uPrefilteredMap;
layout(set = 0, binding = 4) uniform sampler2D uBrdfLut;
layout(set = 0, binding = 5) uniform sampler2DArray uShadowMap;
layout(set = 0, binding = 7) uniform sampler2D uHiZMap;

// Terrain Material Multi-Layer Bindings (Set 1)
layout(set = 1, binding = 0) uniform sampler2D uSplatMap;                  // RGBA weight map: R=Grass, G=Rock, B=Dirt, A=Sand
layout(set = 1, binding = 1) uniform sampler2DArray uTerrainAlbedoArray;   // 4 layers
layout(set = 1, binding = 2) uniform sampler2DArray uTerrainNormalArray;   // 4 layers
layout(set = 1, binding = 3) uniform sampler2DArray uTerrainRoughAoArray;  // 4 layers: R=AO, G=Roughness, B=Metallic, A=Pad

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragColor;
layout(location = 3) in vec2 fragUV;
layout(location = 4) in vec2 fragTerrainUV;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265358979323846;
const float MAX_REFLECTION_LOD = 4.0;

// Surface cotangent frame calculation for tangent-space normal mapping
mat3 cotangentFrame(vec3 N, vec3 p, vec2 uv) {
    vec3 dp1 = dFdx(p);
    vec3 dp2 = dFdy(p);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);

    // In Vulkan, screen Y increases downwards. Using cross(N, dp2) and cross(dp1, N)
    // constructs a correct right-handed tangent frame where T points along +U and B points along +V.
    vec3 dp2perp = cross(N, dp2);
    vec3 dp1perp = cross(dp1, N);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

    float maxLenSq = max(dot(T, T), dot(B, B));
    if (isinf(maxLenSq) || isnan(maxLenSq) || maxLenSq < 1e-10) {
        // Safe robust fallback when UV derivatives are degenerate: construct orthonormal frame around N
        vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        vec3 fallbackT = normalize(cross(up, N));
        vec3 fallbackB = cross(N, fallbackT);
        return mat3(fallbackT, fallbackB, N);
    }
    float invmax = inversesqrt(maxLenSq);
    return mat3(T * invmax, B * invmax, N);
}

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

// Smith geometry
float geometrySmith(float NdotV, float NdotL, float roughness) {
    return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 evaluateLight(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo, float metallic, float roughness, vec3 F0) {
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0001);

    float D = distributionGGX(N, H, roughness);
    float G = geometrySmith(NdotV, NdotL, roughness);
    vec3  F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 specular = (D * G * F) / (4.0 * NdotV * NdotL + 0.0001);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    return (kD * albedo / PI + specular) * radiance * NdotL;
}

// Directional cascade shadow sampling with 3x3 PCF and slope-scaled depth bias
float sampleCascadeShadow(vec3 worldPos, vec3 N, vec3 L, int layer) {
    vec4 fragPosLightSpace = scene.cascadeLightSpaceMatrices[layer] * vec4(worldPos, 1.0);
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;

    if (projCoords.z > 1.0 || projCoords.z < 0.0 ||
        projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 0.0;
    }

    float cosTheta = max(dot(N, L), 0.0);
    float cascadeBiasScale = 1.0 + float(layer) * 0.45;
    float bias = max(scene.shadowParams.y * (1.0 - cosTheta), scene.shadowParams.y * 0.20) * cascadeBiasScale;

    float currentDepth = projCoords.z;
    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(uShadowMap, 0).xy);

    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            vec2 uv = projCoords.xy + vec2(x, y) * texelSize;
            float pcfDepth = texture(uShadowMap, vec3(uv, float(layer))).r;
            shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
}

float calculateCascadeShadow(vec3 worldPos, vec3 N, vec3 L, out int outCascadeIndex, out float outBlendFactor) {
    outCascadeIndex = 0;
    outBlendFactor = 0.0;

    if (scene.shadowParams.x < 0.5) {
        return 0.0;
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

    float blendFraction = scene.cascadeParams.y;
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
    // 1. Sample and normalize Splat weight map (R=Grass, G=Rock, B=Dirt, A=Sand)
    vec4 splat = texture(uSplatMap, fragTerrainUV);
    float weightSum = splat.r + splat.g + splat.b + splat.a;
    if (weightSum > 0.0001) {
        splat /= weightSum;
    } else {
        splat = vec4(1.0, 0.0, 0.0, 0.0);
    }

    // 2. Multi-layer blending across the 4 layers
    vec3 blendedAlbedo = vec3(0.0);
    vec3 blendedNormalTangent = vec3(0.0);
    float roughness = 0.0;
    float metallic = 0.0;
    float ao = 0.0;

    const float layerWeights[4] = float[4](splat.r, splat.g, splat.b, splat.a);

    for (int i = 0; i < 4; ++i) {
        float w = layerWeights[i];
        if (w > 0.001) {
            vec3 layerAlbedo = texture(uTerrainAlbedoArray, vec3(fragUV, float(i))).rgb;
            vec3 rawNorm = texture(uTerrainNormalArray, vec3(fragUV, float(i))).xyz;
            vec4 roughAo = texture(uTerrainRoughAoArray, vec3(fragUV, float(i)));

            vec3 layerNormalTangent = normalize(rawNorm * 2.0 - 1.0);

            blendedAlbedo += layerAlbedo * w;
            blendedNormalTangent += layerNormalTangent * w;
            ao += roughAo.r * w;
            roughness += roughAo.g * w;
            metallic += roughAo.b * w;
        }
    }

    roughness = clamp(roughness, 0.04, 1.0);
    metallic  = clamp(metallic, 0.0, 1.0);
    ao        = clamp(ao, 0.0, 1.0);

    // 3. Surface Normal calculation via cotangent frame
    vec3 geomNormal = normalize(fragNormal);
    vec3 tangentNormal = normalize(blendedNormalTangent);
    mat3 TBN = cotangentFrame(geomNormal, fragWorldPos, fragUV);
    vec3 N = normalize(TBN * tangentNormal);

    vec3 albedo = blendedAlbedo;
    vec3 emissive = vec3(0.0);

    vec3 V = normalize(scene.cameraPos.xyz - fragWorldPos);
    vec3 R = reflect(-V, N);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // 4. Directional Sunlight with CSM Shadows
    vec3 Lo = vec3(0.0);
    vec3 Ls = normalize(scene.sunDirection.xyz);

    int cascadeIndex = 0;
    float blendFactor = 0.0;
    float shadow = calculateCascadeShadow(fragWorldPos, N, Ls, cascadeIndex, blendFactor);
    float shadowFactor = 1.0 - shadow;

    vec3 sunDirect = evaluateLight(N, V, Ls, scene.sunColor.rgb * scene.sunColor.a, albedo, metallic, roughness, F0);
    Lo += sunDirect * shadowFactor;

    // 5. Point Lights
    for (int i = 0; i < 2; ++i) {
        vec3 toLight = scene.pointPosIntensity[i].xyz - fragWorldPos;
        float dist2 = max(dot(toLight, toLight), 0.01);
        vec3 L = toLight * inversesqrt(dist2);
        vec3 radiance = scene.pointColor[i].rgb * scene.pointPosIntensity[i].w / dist2;
        Lo += evaluateLight(N, V, L, radiance, albedo, metallic, roughness, F0);
    }

    // 6. Image-Based Lighting (IBL)
    float NdotV = max(dot(N, V), 0.0);
    vec3 F = fresnelSchlickRoughness(NdotV, F0, roughness);
    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

    vec3 irradiance = texture(uIrradianceMap, N).rgb;
    vec3 diffuseIBL = irradiance * albedo;

    vec3 prefilteredColor = textureLod(uPrefilteredMap, R, roughness * MAX_REFLECTION_LOD).rgb;
    vec2 brdf = texture(uBrdfLut, vec2(NdotV, roughness)).rg;
    vec3 specularIBL = prefilteredColor * (F * brdf.x + brdf.y);

    vec3 ambient = (kD * diffuseIBL + specularIBL) * ao;

    // -----------------------------------------------------------------
    // Viewport Rendering Modes Evaluation (Milestone 17)
    // -----------------------------------------------------------------
    uint mode = uint(pc.viewportParams.x + 0.5);
    uint normalSpace = uint(pc.viewportParams.y + 0.5);
    float nearPlane = pc.viewportParams.z;
    float farPlane = pc.viewportParams.w;

    // Mode 1: Unlit
    if (mode == 1u) {
        outColor = vec4(albedo, 1.0);
        return;
    }

    // Mode 2: Wireframe
    if (mode == 2u) {
        outColor = vec4(0.20, 0.95, 0.35, 1.0);
        return;
    }

    // Mode 3: Solid (Studio / Lambertian shading without textures)
    if (mode == 3u) {
        float NdotL = clamp(dot(N, Ls) * 0.7 + 0.3, 0.0, 1.0);
        outColor = vec4(vec3(0.72) * NdotL, 1.0);
        return;
    }

    // Mode 4: Material Preview (Ambient IBL only)
    if (mode == 4u) {
        vec3 previewColor = ambient;
        previewColor = previewColor / (previewColor + vec3(1.0));
        outColor = vec4(previewColor, 1.0);
        return;
    }

    // Mode 5: Normals
    if (mode == 5u) {
        vec3 displayNormal = (normalSpace == 1u) ? normalize(mat3(scene.view) * N) : N;
        outColor = vec4(displayNormal * 0.5 + 0.5, 1.0);
        return;
    }

    // Mode 6: Albedo
    if (mode == 6u) {
        outColor = vec4(albedo, 1.0);
        return;
    }

    // Mode 7: Roughness
    if (mode == 7u) {
        outColor = vec4(vec3(roughness), 1.0);
        return;
    }

    // Mode 8: Metallic
    if (mode == 8u) {
        outColor = vec4(vec3(metallic), 1.0);
        return;
    }

    // Mode 9: Ambient Occlusion
    if (mode == 9u) {
        outColor = vec4(vec3(ao), 1.0);
        return;
    }

    // Mode 10: Depth
    if (mode == 10u) {
        float z_n = gl_FragCoord.z;
        float near = max(nearPlane, 0.01);
        float far = max(farPlane, 1.0);
        float linearDepth = (near * far) / (far - z_n * (far - near));
        float normDepth = clamp((linearDepth - near) / (far - near), 0.0, 1.0);
        outColor = vec4(vec3(pow(normDepth, 0.45)), 1.0);
        return;
    }

    // Mode 11: UV Coordinates
    if (mode == 11u) {
        vec2 uvFrac = fract(fragUV * 0.5);
        float gridEdge = (uvFrac.x < 0.03 || uvFrac.y < 0.03) ? 1.0 : 0.0;
        vec3 uvCol = mix(vec3(fract(fragUV.x), fract(fragUV.y), 0.5), vec3(1.0), gridEdge * 0.7);
        outColor = vec4(uvCol, 1.0);
        return;
    }

    // Mode 12: Emissive
    if (mode == 12u) {
        outColor = vec4(vec3(0.0), 1.0);
        return;
    }

    // Mode 13: Shadow Debug
    if (mode == 13u) {
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
        vec3 shadowVis = mix(Lo + ambient, tint, 0.55);
        shadowVis = shadowVis / (shadowVis + vec3(1.0));
        outColor = vec4(shadowVis, 1.0);
        return;
    }

    // Mode 14: Face Orientation (Front-facing vs Back-facing / Inverted normals)
    if (mode == 14u) {
        float NdotV = dot(N, V);
        if (!gl_FrontFacing) {
            outColor = vec4(0.15, 0.45, 0.95, 1.0); // Blue for back-facing primitives
        } else if (NdotV < 0.0) {
            outColor = vec4(0.95, 0.15, 0.15, 1.0); // Bright Red for inverted surface normals!
        } else {
            float shade = clamp(NdotV * 0.6 + 0.4, 0.0, 1.0);
            outColor = vec4(vec3(0.15, 0.85, 0.35) * shade, 1.0); // Green for correct front-facing
        }
        return;
    }

    // Default: Mode 0 (Lit) -> Full PBR + Direct + IBL + Tone mapping
    vec3 color = Lo + ambient + emissive;
    color = color / (color + vec3(1.0));

    // Handle shadow overlay debug modes if active
    int shadowDbg = int(scene.shadowParams.w + 0.5);
    if (shadowDbg == 4) {
        outColor = vec4(vec3(shadowFactor), 1.0);
        return;
    }

    outColor = vec4(color, 1.0);
}
