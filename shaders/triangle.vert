#version 450

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 view;
    mat4 proj;
    mat4 viewProj;
    vec4 cameraPos;
    vec4 sunDirection;     // xyz = direction TOWARDS the sun
    vec4 sunColor;         // rgb = color, a = intensity
    vec4 pointPosIntensity[2];  // xyz = world position, w = intensity
    vec4 pointColor[2];         // rgb = color
} scene;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 albedoMetallic;
    vec4 roughnessAoTex;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inNormal;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragColor;
layout(location = 3) out vec2 fragUV;

void main() {
    vec4 world = pc.model * vec4(inPosition, 1.0);
    fragWorldPos = world.xyz;
    // Inverse-transpose keeps normals correct under non-uniform scale
    fragNormal = normalize(transpose(inverse(mat3(pc.model))) * inNormal);
    fragColor = inColor;
    fragUV = inUV;
    gl_Position = scene.viewProj * world;
}
