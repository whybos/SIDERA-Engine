#version 450

const float PI = 3.14159265359;
const uint MAX_LIGHTS_PER_TILE = 64u;

struct GPULight {
    vec4 positionRadius;
    vec4 directionType;
    vec4 colorIntensity;
    vec4 spotParams;
};

layout(set = 0, binding = 0) uniform sampler2D uGBufferAlbedo;
layout(set = 0, binding = 1) uniform sampler2D uGBufferNormal;
layout(set = 0, binding = 2) uniform sampler2D uGBufferMaterial; // R=roughness, G=metallic, B=ao
layout(set = 0, binding = 3) uniform sampler2D uGBufferEmissive;

layout(std430, set = 0, binding = 4) readonly buffer LightBuffer {
    GPULight lights[];
};
layout(std430, set = 0, binding = 5) readonly buffer TileLightIndexBuffer {
    uint tileLightIndices[];
};
layout(std430, set = 0, binding = 6) readonly buffer TileLightCountBuffer {
    uint tileLightCounts[];
};

layout(push_constant) uniform DeferredLightPC {
    vec4 cameraPosTilesX; // xyz = cameraPos, w = tilesX
    vec4 debugParams;     // x = debugMode (0=Lit,1=Albedo,2=Normal,3=Rough,4=Metal,5=AO,6=Emissive,7=TileHeatmap)
} pc;

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-5);
}

float geometrySchlickGGX(float NdotX, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotX / max(NdotX * (1.0 - k) + k, 1e-5);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    vec3 albedo   = texture(uGBufferAlbedo, inUV).rgb;
    vec3 N        = normalize(texture(uGBufferNormal, inUV).xyz);
    vec3 matData  = texture(uGBufferMaterial, inUV).rgb;
    vec3 emissive = texture(uGBufferEmissive, inUV).rgb;

    float roughness = clamp(matData.r, 0.04, 1.0);
    float metallic  = clamp(matData.g, 0.0, 1.0);
    float ao        = clamp(matData.b, 0.0, 1.0);

    uint dbg = uint(pc.debugParams.x + 0.5);
    if (dbg == 1u) { outColor = vec4(albedo, 1.0); return; }
    if (dbg == 2u) { outColor = vec4(N * 0.5 + 0.5, 1.0); return; }
    if (dbg == 3u) { outColor = vec4(vec3(roughness), 1.0); return; }
    if (dbg == 4u) { outColor = vec4(vec3(metallic), 1.0); return; }
    if (dbg == 5u) { outColor = vec4(vec3(ao), 1.0); return; }
    if (dbg == 6u) { outColor = vec4(emissive, 1.0); return; }

    uvec2 tileCoord = uvec2(gl_FragCoord.xy) / 16u;
    uint tilesX = max(1u, uint(pc.cameraPosTilesX.w + 0.5));
    uint tileIdx = tileCoord.y * tilesX + tileCoord.x;
    uint lightCount = min(tileLightCounts[tileIdx], MAX_LIGHTS_PER_TILE);

    if (dbg == 7u) {
        float heat = clamp(float(lightCount) / 16.0, 0.0, 1.0);
        outColor = vec4(heat, 1.0 - abs(heat - 0.5) * 2.0, 1.0 - heat, 1.0);
        return;
    }

    vec3 V = normalize(pc.cameraPosTilesX.xyz);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 Lo = vec3(0.0);

    uint baseOffset = tileIdx * MAX_LIGHTS_PER_TILE;
    for (uint i = 0u; i < lightCount; ++i) {
        GPULight L = lights[tileLightIndices[baseOffset + i]];
        uint lType = uint(L.directionType.w + 0.5);
        vec3 lightDir = normalize(-L.directionType.xyz);
        float atten = 1.0;
        if (lType != 0u) {
            lightDir = normalize(L.positionRadius.xyz);
            atten = 1.0;
        }
        vec3 H = normalize(V + lightDir);
        float NdotL = max(dot(N, lightDir), 0.0);
        float NdotV = max(dot(N, V), 0.001);
        float D = distributionGGX(N, H, roughness);
        float G = geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
        vec3  F = fresnelSchlick(max(dot(H, V), 0.0), F0);
        vec3 spec = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);
        vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
        Lo += (kD * albedo / PI + spec) * (L.colorIntensity.rgb * L.colorIntensity.a * atten) * NdotL;
    }

    vec3 color = Lo + albedo * 0.08 * ao + emissive;
    color = color / (color + vec3(1.0));
    outColor = vec4(color, 1.0);
}
