#pragma once

#include "constants.slang"
#include "material.slang"

#define VECX float3(1.0f, 0.0f, 0.0f)
#define VECY float3(0.0f, 0.0f, 1.0f)

inline float schlickFresnel(float F0, float VdotH) { return F0 + (1.0f - F0) * pow(1.0f - VdotH, 5.0f); }
inline float schlickWeight(float f) { float m = clamp(1.0f - f, 0.0f, 1.0f); return m * m * m * m * m; } //pow(m, 5.0f)
inline float square(float f) { return f * f; }

float GTR1(float NdotH, float a) {
    if (a >= 1.0f) return ONE_OVER_PI;
    float a2 = a * a;
    return (a2 - 1.0f) / (PI * log2(a2) * (1.0f + (a2 - 1.0f) * NdotH * NdotH));
}

float GTR2(float NdotH, float a) {
    float a2 = a * a;
    return a2 / (PI * (1.0f + (a2 - 1.0f) * NdotH * NdotH));
}

float GTR2_anisotropic(float NdotH, float HdotX, float HdotY, float2 a) {
    return 1 / (PI * a.x * a.y * square(square(HdotX/a.x) + square(HdotY/a.y) + NdotH * NdotH));
}

float GGX(float NdotV, float a) {
    float a2 = a * a;
    return 2.0f / (1.0f + sqrt(a2 + (1.0f - a2) * NdotV * NdotV));
}

float GGX_anisotropic(float NdotV, float VdotX, float VdotY, float2 a) {
    return 1 / (NdotV + sqrt(square(VdotX * a.x) + square(VdotY * a.y) * NdotV * NdotV));
}

float3 calculateTint(float3 color) {
    float l = dot(float3(0.3f, 0.6f, 1.0f), color);
    return select(l > 0.0f, color * (1.0f / l), float3(1.0f));
}

float3 evalSheen(Material *material, float HdotL) {
    float3 tint = calculateTint(material.color);
    return lerp(float3(1.0f), tint, material.sheenTint) * schlickWeight(HdotL);
}

float3 evalClearcoat(Material *material, float NdotH, float NdotL, float NdotV, float LdotH) {
    float d = GTR1(NdotH, lerp(0.1f, 0.001f, material.clearCoatGloss));
    float f = schlickFresnel(0.04f, LdotH);
    float g = GGX(NdotL, 0.25f) * GGX(NdotV, 0.25f);

    return 0.25f * material.clearCoat * d * f * g;
}

float evalDiffuse(Material *material, float3 N, float3 L, float3 V, float3 H) {
    float FL = schlickWeight(dot(N, L));
    float FV = schlickWeight(dot(N, V));

    float FD90 = 0.5 + 2 * material.roughness * square(dot(H, L));
    float FD = lerp(1.0f, FD90, FL) * lerp(1.0f, FD90, FV);

    float Fss90 = square(dot(L, H)) * material.roughness;
    float Fss = lerp(1.0f, Fss90, FL) * lerp(1.0f, Fss90, FV);
    float ss = 1.25f * (Fss * (1 / (dot(N, L) + dot(N, V)) - 0.5f) + 0.5f);

    return lerp(FD, ss, material.subsurface);
}

float2 calculateAnisotropicParameters(float2 p) {
    float aspect = sqrt(1.0f - p.x * 0.9f);
    float r2 = p.y * p.y;
    return float2(
        max(0.001f, r2 / aspect), 
        max(0.001f, r2 * aspect));
}

float3 evalSpecular(Material *material, float NdotH, float NdotL, float NdotV, float3 H, float3 V, float3 L) {
    float2 p = float2(material.anisotropic, material.roughness);
    float2 anisotropicParameters = calculateAnisotropicParameters(p);

    float3 tint = calculateTint(material.color);
    float3 color = lerp(material.specular * 0.08f * lerp(float3(1.0f), tint, material.specularTint), material.color, material.metallic);

    float d = GTR2_anisotropic(NdotH, dot(H, VECX), dot(H, VECY), anisotropicParameters);
    float fresnel = schlickWeight(dot(L, H));
    float3 f = lerp(color, float3(1.0f), fresnel);
    float g = GGX_anisotropic(NdotL, dot(L, VECX), dot(L, VECY), anisotropicParameters) * GGX_anisotropic(NdotV, dot(V, VECX), dot(V, VECY), anisotropicParameters);

    return d * f * g;
}

float3 BRDF(Material *material, float3 N, float3 V, float3 L) {
    float NdotL = dot(N, L);
    float NdotV = dot(N, V);

    if (NdotL <= 0.0f || NdotV <= 0.0f)
        return float3(0.0f);

    float3 H = normalize(V + L);
    float NdotH = dot(N, H);
    float HdotL = dot(H, L);

    float3 sheen = evalSheen(material, HdotL);
    float3 clearCoat = evalClearcoat(material, NdotH, NdotL, NdotV, HdotL);
    float3 specular = evalSpecular(material, NdotH, NdotL, NdotV, H, V, L);
    float diffuse = evalDiffuse(material, N, L, V, H);

    return (ONE_OVER_PI * diffuse * material.color + sheen) * (1 - material.metallic) + specular + clearCoat;
}