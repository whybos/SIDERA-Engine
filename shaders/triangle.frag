#version 450

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 view;
    mat4 proj;
    mat4 viewProj;
    vec4 cameraPos;
    vec4 sunDirection;
    vec4 sunColor;
    vec4 pointPosIntensity[2];
    vec4 pointColor[2];
} scene;

layout(set = 0, binding = 1) uniform sampler2D uAlbedoMap;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 albedoMetallic;   // rgb = albedo tint, a = metallic
    vec4 roughnessAoTex;   // x = roughness, y = ao, z = texture strength
} pc;

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragColor;
layout(location = 3) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;

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

// Cook-Torrance radiance contribution of a single light
vec3 evaluateLight(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo, float metallic, float roughness, vec3 F0) {
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0001);

    float D = distributionGGX(N, H, roughness);
    float G = geometrySmith(NdotV, NdotL, roughness);
    vec3  F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 specular = (D * G * F) / (4.0 * NdotV * NdotL + 0.0001);

    // Energy conservation: what is reflected specularly is not diffused; metals have no diffuse
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    return (kD * albedo / PI + specular) * radiance * NdotL;
}

void main() {
    float metallic  = clamp(pc.albedoMetallic.a, 0.0, 1.0);
    float roughness = clamp(pc.roughnessAoTex.x, 0.04, 1.0);
    float ao        = clamp(pc.roughnessAoTex.y, 0.0, 1.0);

    vec3 texAlbedo = mix(vec3(1.0), texture(uAlbedoMap, fragUV).rgb, pc.roughnessAoTex.z);
    vec3 albedo = pc.albedoMetallic.rgb * fragColor * texAlbedo;

    vec3 N = normalize(fragNormal);
    vec3 V = normalize(scene.cameraPos.xyz - fragWorldPos);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3 Lo = vec3(0.0);

    // Directional sun
    vec3 Ls = normalize(scene.sunDirection.xyz);
    Lo += evaluateLight(N, V, Ls, scene.sunColor.rgb * scene.sunColor.a, albedo, metallic, roughness, F0);

    // Point lights with inverse-square falloff
    for (int i = 0; i < 2; ++i) {
        vec3 toLight = scene.pointPosIntensity[i].xyz - fragWorldPos;
        float dist2 = max(dot(toLight, toLight), 0.01);
        vec3 L = toLight * inversesqrt(dist2);
        vec3 radiance = scene.pointColor[i].rgb * scene.pointPosIntensity[i].w / dist2;
        Lo += evaluateLight(N, V, L, radiance, albedo, metallic, roughness, F0);
    }

    // Hemisphere ambient (sky above, ground bounce below) for diffuse + a rough specular term
    const vec3 skyColor = vec3(0.30, 0.42, 0.62);
    const vec3 groundColor = vec3(0.10, 0.09, 0.08);
    vec3 R = reflect(-V, N);
    vec3 F = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 irradiance = mix(groundColor, skyColor, N.y * 0.5 + 0.5);
    vec3 envSpec = mix(groundColor, skyColor, R.y * 0.5 + 0.5);
    vec3 ambient = (kD * irradiance * albedo + F * envSpec * (1.0 - 0.6 * roughness)) * ao;

    vec3 color = Lo + ambient;

    // Reinhard tone mapping (output is written to an sRGB swapchain, which applies the gamma curve)
    color = color / (color + vec3(1.0));

    outColor = vec4(color, 1.0);
}
