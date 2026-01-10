#define PI 3.1415926535897F
#define PI_2 6.2831853071795F
#define N_1_PI 0.3183098861837F

struct BsdfMaterial {
    float3 baseColor = float3(1.0f, 1.0f, 1.0f);
    float2 roughness = float2(1.0f, 1.0f);
    float metallic = 1.0f;
    float3 emissive = float3(0.0f, 0.0f, 0.0f);

    float3 N = float3(0.0f, 0.0f, 1.0f);
    float3 Ng = float3(0.0f, 0.0f, 1.0f);
    float3 T = float3(1.0f, 0.0f, 0.0f);
    float3 B = float3(0.0f, 1.0f, 0.0f);
}

struct BsdfEvaluateData {
    float3 k1;
    float3 k2;
    float3 xi;
    float3 bsdf_diffuse;
    float3 bsdf_glossy;
    float pdf;
}

inline void orthonormalBasis(float3 normal, out float3 tangent, out float3 bitangent) {
    if (normal.z < -0.99998796F) {
        tangent = float3(0.0F, -1.0F, 0.0F);
        bitangent = float3(-1.0F, 0.0F, 0.0F);
        return;
    }

    float a = 1.0F / (1.0F + normal.z);
    float b = -normal.x * normal.y * a;
    tangent = float3(1.0F - normal.x * normal.y * a, b, -normal.x);
    bitangent = float3(b, 1.0F - normal.y * normal.y * a, -normal.y);
}

float clampedDot(float3 a, float3 b) {
    return clamp(dot(a, b), 0.0F, 1.0F);
}

float schlickFresnel(float F0, float F90, float VdotH) {
    return F0 + (F90 - F0) * pow(1.0F - VdotH, 5.0F);
}

float3 schlickFresnel(float3 F0, float3 F90, float VdotH) {
    return F0 + (F90 - F0) * pow(1.0F - VdotH, 5.0F);
}

float bsdfSimpleGlossyProbability(float NdotV, float metallic) {
    return lerp(schlickFresnel(0.04, 1.0, NdotV), 1.0, metallic);
}

float hvd_ggx_eval(const float2 invRoughness, const float3 h) {
    const float x = h.x * invRoughness.x;
    const float y = h.y * invRoughness.y;
    const float aniso = x * x + y * y;
    const float f = aniso + h.z * h.z;

    return N_1_PI * invRoughness.x * invRoughness.y * h.z / (f * f);
}

float smith_shadow_or_mask(const float3 k, const float2 roughness)
{
    float kz2 = k.z * k.z;
    if (kz2 == 0.0f)
    {
        return 0.0f;
    }
    const float ax = k.x * roughness.x;
    const float ay = k.y * roughness.y;
    const float inv_a2 = (ax * ax + ay * ay) / kz2;

    return 2.0f / (1.0f + sqrt(1.0f + inv_a2));
}

float ggx_smith_shadow_mask(out float G1, out float G2, const float3 k1, const float3 k2, const float2 roughness) {
    G1 = smith_shadow_or_mask(k1, roughness);
    G2 = smith_shadow_or_mask(k2, roughness);

    return G1 * G2;
}

void bsdfEvaluateSimple(inout BsdfEvaluateData data, BsdfMaterial mat)
{
    // Specular reflection
    float3 H = normalize(data.k1 + data.k2);
    float NdotV = clampedDot(mat.N, data.k1);
    float NdotL = clampedDot(mat.N, data.k2);
    float VdotH = clampedDot(data.k1, H);
    float NdotH = clampedDot(mat.N, H);

    if (NdotV == 0.0f || NdotL == 0.0f || VdotH == 0.0f || NdotH == 0.0f)
    {
        data.bsdf_diffuse = data.bsdf_glossy = float3(0.0f);
        data.pdf = 0.0f;
        return;
    }

    // We combine the metallic and specular lobes into a single glossy lobe.
    // The metallic weight is     metallic *    fresnel(f0 = baseColor)
    // The specular weight is (1-metallic) *    fresnel(f0 = c_min_reflectance)
    // The diffuse weight is  (1-metallic) * (1-fresnel(f0 = c_min_reflectance)) * baseColor

    // Fresnel terms
    float c_min_reflectance = 0.04F;
    float3 f0 = lerp(float3(c_min_reflectance), mat.baseColor, mat.metallic);
    float3 fGlossy = schlickFresnel(f0, float3(1.0F), VdotH); // Metallic + specular
    float fDiffuse = schlickFresnel(1.0F - c_min_reflectance, 0.0F, VdotH) * (1.0F - mat.metallic);

    // Specular GGX
    float3 localH = float3(dot(mat.T, H), dot(mat.B, H), NdotH);
    float d = hvd_ggx_eval(1.0f / mat.roughness, localH);
    float3 localK1 = float3(dot(mat.T, data.k1), dot(mat.B, data.k1), NdotV);
    float3 localK2 = float3(dot(mat.T, data.k2), dot(mat.B, data.k2), NdotL);
    float G1 = 0.0f, G2 = 0.0f;
    ggx_smith_shadow_mask(G1, G2, localK1, localK2, mat.roughness);

    float diffusePdf = N_1_PI * NdotL;
    float specularPdf = G1 * d * 0.25f / (NdotV * NdotH);
    data.pdf = lerp(diffusePdf, specularPdf, bsdfSimpleGlossyProbability(NdotV, mat.metallic));

    data.bsdf_diffuse = mat.baseColor * fDiffuse * diffusePdf; // Lambertian
    data.bsdf_glossy = fGlossy * G2 * specularPdf;             // GGX-Smith
}

float3 pbrMetallicRoughness(float3 albedo, float metallic, float roughness, float3 N, float3 V, float3 L) {
    BsdfEvaluateData data;
    data.k1 = V;
    data.k2 = L;
    data.xi = float3(0.0f);

    BsdfMaterial mat;
    mat.baseColor = albedo;
    mat.metallic = metallic;
    mat.roughness = float2(roughness, roughness);
    mat.N = N;
    orthonormalBasis(mat.N, mat.T, mat.B);
    bsdfEvaluateSimple(data, mat);

    return data.bsdf_diffuse + data.bsdf_glossy;
}