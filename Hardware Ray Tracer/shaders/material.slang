#pragma once

struct Material {
    float3 color;
    float subsurface;
    float metallic;
    float roughness;
    float specular;
    float specularTint;
    float anisotropic;
    float sheen;
    float sheenTint;
    float clearCoat;
    float clearCoatGloss;
};