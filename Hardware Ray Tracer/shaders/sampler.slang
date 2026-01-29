#pragma once
#include "random.slang"
#include "material.slang"
#include "constants.slang"
#include "shadermath.slang"

// -------------------- HELPER FUNCTIONS --------------------
float calculateSpecularProbability() { return 0.0f; }
float lambda(float3 vec, float2 ani) {
    if (vec.z <= 0.0f) return 0.0f;

    float cosTheta2 = vec.z * vec.z;
    float sinTheta2 = max(0.0f, 1.0f - cosTheta2);
    float tanTheta2 = sinTheta2 / cosTheta2;

    float cosPhi2 = select(sinTheta2 > 0.0f, clamp(vec.x * vec.x / sinTheta2, 0.0f, 1.0f), 1.0f);
    float sinPhi2 = 1.0f - cosPhi2;

    float a2 = cosPhi2 * ani.x * ani.y + sinPhi2 * ani.x * ani.y;
    return 0.5f * (-1.f + sqrt(1.0f + a2 * tanTheta2));
}

float GGXVNDFPDF(Material *material, float3 wo, float3 wi) {
    float a2 = square(material.roughness);
    float NdotL = wi.z;
    float NdotV = wo.z;

    float f1 = sqrt(a2 + (1.0f - a2) * NdotL * NdotL);
    float f2 = sqrt(a2 + (1.0f - a2) * NdotV * NdotV);
    float G1 = 2.0f * NdotV / sqrt(a2 + (1.0f - a2) * NdotV * NdotV) + NdotV;
    float G2 = 2.0f * NdotL * NdotV / (f1 + f2);
    return (G2 / G1);
}

float2 anisotropicFromMaterial(Material *material) {
    float aspect = sqrt(1.0f - material.anisotropic * 0.9f);
    float r2 = square(material.roughness);
    return float2(
        max(0.001f, r2 / aspect),
        max(0.001f, r2 * aspect)
    );
}
// -------------------- SAMPLING FUNCTIONS --------------------
float3 sample(Material *material, float3 N, float3 V, uint seed, out float pdf) {
    float specProb = calculateSpecularProbability();
    float2 randoms = float2(rand(seed), rand(seed));

    if (rand(seed) < specProb) //decide if the ray should be a specular ray
        return sampleGGXVNDFSphericalCap(material, V, N, randoms, pdf); //sample specular ray
    else return sampleCosineWeightedHemisphere(randoms, pdf); //sample diffuse ray
}

float3 sampleCosineWeightedHemisphere(float2 randoms, out float pdf) {
    float phi = TWO_PI * randoms.y;
    float cosTheta = sqrt(randoms.x);
    float sinTheta = sqrt(max(0.0f, 1.0f - cosTheta * cosTheta));

    pdf = 1.0f / (cosTheta * ONE_OVER_PI);

    return float3(
        sinTheta * cos(phi),
        sinTheta * sin(phi),
        cosTheta
    );
}

float3 sampleGGXVNDFSphericalCap(Material *material, float3 V, float3 N, float2 randoms, out float pdf) {
    float3 wo = toLocal(V, N);
    float2 anisotropic = anisotropicFromMaterial(material);

    float3 v = normalize(float3(anisotropic.x * -wo.x, anisotropic.y * -wo.y, -wo.z));

    float lensq = square(v.x) + square(v.y);
    float3 t1 = select(lensq > 0.0f, float3(-v.y, v.x, 0.0f) * rsqrt(lensq), float3(1, 0, 0));
    float3 t2 = cross(v, t1);

    float r = sqrt(randoms.x);
    float phi = TWO_PI * randoms.y;
    float p1 = r * cos(phi);
    float p2 = r * sin(phi);
    float s = 0.5f * (1.0f + v.z);
    p2 = (1.0f - s) * sqrt(1.0f - square(p1)) + s * p2;

    float3 n = t1 * p1 + t2 * p2 + sqrt(max(0.0f, 1.0f - square(p1) - square(p2))) * v;
    float3 wm = normalize(float3(anisotropic.x * n.x, anisotropic.y * n.y, max(0.0f, n.z)));

    float3 wi = reflect(wo, wm);

    if (wi.z < 0.0f)
        pdf = 0.0f;
    else pdf = GGXVNDFPDF(material, wo, wi) * 4.0f;
    return toWorld(wi, N);
}