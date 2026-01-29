#pragma once

inline float square(float f) { return f * f; }

void orthonormalBasis(float3 normal, out float3 tangent, out float3 bitangent) {
    if (normal.z < -0.99998796F) {
        tangent = float3(0.0F, -1.0F, 0.0F);
        bitangent = float3(-1.0F, 0.0F, 0.0F);
        return;
    }

    float a = 1.0F / (1.0F + normal.z);
    float b = -normal.x * normal.y * a;
    tangent = float3(1.0F - normal.x * normal.x * a, b, -normal.x);
    bitangent = float3(b, 1.0F - normal.y * normal.y * a, -normal.y);
}

float3 toLocal(float3 vec, float3 normal) {
    float3 tangent, bitangent;
    orthonormalBasis(normal, tangent, bitangent);

    return float3(dot(vec, tangent), dot(vec, bitangent), dot(vec, normal));
}

float3 toWorld(float3 vec, float3 normal) {
    float3 tangent, bitangent;
    orthonormalBasis(normal, tangent, bitangent);

    return vec.x * tangent + vec.y * bitangent + vec.z * normal;
}