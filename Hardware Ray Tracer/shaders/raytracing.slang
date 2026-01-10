#include "pbr.slang"
#include "random.slang"
#include "objects.slang"
#include "light.slang"
#include "constants.slang"

struct UniformBuffer {
    float4x4 viewInverse;
    float4x4 projInverse;
    uint32_t frame;
    uint32_t depthMax;
};

struct SceneInfo {
    uint64_t materialBuffer;
    uint64_t materialByteStride;

    uint64_t lightBuffer;
    uint64_t lightByteStride;
    uint64_t numLights;

    uint64_t vertexByteStride;

    uint64_t instanceBuffer;
    uint64_t instanceByteStride;

    uint64_t skyBuffer;
    uint64_t skyStride;
};

RaytracingAccelerationStructure topLevelAS;
RWTexture2D<float4> outImage;
ConstantBuffer<UniformBuffer> uniformBuffer;
GLSLShaderStorageBuffer<SceneInfo> sceneInfo;

struct HitPayload {
    float3 color;
    float weight;
    int depth;

    float3 rayOrigin;
    float3 rayDirection;
};

struct ShadowPayload {
    int depth;
}

__generic<T : IFloat> T getAttribute(uint64_t bufferAddress, uint64_t offset, uint64_t byteStride, uint attributeIndex) {
    T *ptr = (T *)(bufferAddress + offset + attributeIndex * byteStride);
    return ptr[0];
}

int3 getTriangleIndices(uint64_t bufferAddress, int primitiveID) {
    // Indices are always 32-bit format
    int3 *indices = (int3 *)(bufferAddress);
    return indices[primitiveID];
}

uint32_t getMaterialIndex(uint64_t bufferAddress, uint64_t byteStride, uint instance) {
    uint32_t* ptr = (uint32_t*)(bufferAddress + byteStride * instance + 16);
    return ptr[0];
}

__generic<T : IFloat> T getTriangleAttribute(uint64_t bufferAddress, uint64_t offset, uint64_t byteStride, uint3 attributeIndex, float3 barycentrics) {

    T attr0 = getAttribute<T>(bufferAddress, offset, byteStride, attributeIndex.x);
    T attr1 = getAttribute<T>(bufferAddress, offset, byteStride, attributeIndex.y);
    T attr2 = getAttribute<T>(bufferAddress, offset, byteStride, attributeIndex.z);

    return T(barycentrics.x) * attr0 + T(barycentrics.y) * attr1 + T(barycentrics.z) * attr2;
}

uint64_t getIndexBuffer(uint64_t id, uint64_t bufferAddress, uint64_t byteStride) {
    uint64_t *ptr = (uint64_t *)(bufferAddress + id * byteStride + 8); //first 8 bytes are the vertex buffer address
    return ptr[0];
}
uint64_t getVertexBuffer(uint64_t id, uint64_t bufferAddress, uint64_t byteStride) {
    uint64_t *ptr = (uint64_t *)(bufferAddress + id * byteStride);
    return ptr[0];
}

/*
 * Checks if a point can "see" the light and therefore should receive direct illumination
 * TODO: Add multiple shadow rays to better estimate the shadow factor
 */
float testShadow(float3 worldPos, float3 worldNormal, float3 lightDirection, bool directional) {
    RayDesc shadowRay;
    shadowRay.Origin = worldPos + worldNormal * 0.0001;
    shadowRay.Direction = normalize(lightDirection);
    shadowRay.TMin = 0.001;
    shadowRay.TMax = directional ? INFINITE : length(lightDirection);

    ShadowPayload shadowPayload;
    shadowPayload.depth = 0;

    //uses shadow miss shader to reduce the payload and computational cost
    TraceRay(topLevelAS, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xff, 0, 0, 2, shadowRay, shadowPayload);

    return shadowPayload.depth != MISS_DEPTH ? 0.0 : 1.0;
}

/*
 * Currently light calculations only accept one light source
 * Later a light source will cast multiple rays to better determine the illumination of a pixel
 */
#define MAX_LIGHT_SAMPLES 2 //number of lights the program samples per sample per pixel (more light samples result in better lightning quality but increases the computational cost)
float3 calculateColor(Material mat, float3 normal, float3 view, float3 worldPos) {
    uint maxIndex = (uint)(sceneInfo.numLights - 1U);
    uint seed = uniformBuffer.frame;
    uint numLights = (uint) sceneInfo.numLights;
    float3 accumulatedColor = float3(0, 0, 0);
    float lightFactor = 1 / sceneInfo.numLights;
    
    
    /*
     * Sample random light sources
     * This will later be replaced with an more advanced algorithm like light bounding volume hierarchy or ReSTIR
     */
    for (uint i = 0; i < numLights; i++) {
        uint randomIndex = srand(seed + i, maxIndex); //calculate random light index
        Light light = processLight(sceneInfo.lightBuffer, sceneInfo.lightByteStride, i, worldPos); // get light information
        float3 L = normalize(light.direction);
        float3 color = pbrMetallicRoughness(mat.color, mat.metallic, mat.roughness, normal, view, L);
        float shadowFactor = testShadow(worldPos, normal, light.direction, false);
        accumulatedColor += color * light.color * light.intensity * shadowFactor;
    }

    return accumulatedColor;
}

[shader("raygeneration")]
void rgenMain() {
    float2 launchID = (float2)DispatchRaysIndex().xy;
    float2 launchSize = (float2)DispatchRaysDimensions().xy;
    const uint rayFlags = 0;

    uint seed = hash(uint3(uint2(launchID.xy), uniformBuffer.frame));
    float2 subpixel_jitter = uniformBuffer.frame == 0 ? float2(0.5f, 0.5f) : float2(rand(seed), rand(seed));
    const float2 pixelCenter = launchID + subpixel_jitter;

    const float2 clipCoords = launchID / launchSize * 2.0 - 1.0;
    const float4 viewCoords = mul(float4(clipCoords, 1.0, 1.0), uniformBuffer.projInverse);

    RayDesc ray;
    ray.Origin = mul(float4(0.0, 0.0, 0.0, 1.0), uniformBuffer.viewInverse).xyz;
    ray.Direction = mul(float4(normalize(viewCoords.xyz), 0.0), uniformBuffer.viewInverse).xyz;
    ray.TMin = 0.001;
    ray.TMax = INFINITE;

    HitPayload payload;
    payload.color = float3(0, 0, 0);
    payload.weight = 1;
    payload.depth = 0;

    /*
     * The program uses hardware recursion to calculate indirect illumination and reflections
     */
    TraceRay(topLevelAS, rayFlags, 0xff, 0, 0, 0, ray, payload);
    float3 color = payload.color;
    outImage[int2(launchID)] = float4(color, 1.0);
}

[shader("closesthit")]
void rchitMain(inout HitPayload payload, in BuiltInTriangleIntersectionAttributes attr) {
    float3 barycentrics = float3(1 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);

    uint instanceID = InstanceIndex();
    uint meshID = InstanceID();
    uint triID = PrimitiveIndex();

    uint64_t indexBuffer = getIndexBuffer(meshID, sceneInfo.instanceBuffer, sceneInfo.instanceByteStride);
    uint64_t vertexBuffer = getVertexBuffer(meshID, sceneInfo.instanceBuffer, sceneInfo.instanceByteStride);

    int materialIndex = getMaterialIndex(sceneInfo.instanceBuffer, sceneInfo.instanceByteStride, instanceID);
    int3 indices = getTriangleIndices(indexBuffer, triID);
    float3 pos;
    float3 normal;
    float2 uvWorld;

    getTriangleInformation(vertexBuffer, sceneInfo.vertexByteStride, indices, barycentrics, pos, normal, uvWorld);

    float3 worldPos = float3(mul(float4(pos, 1.0), ObjectToWorld4x3()));
    float3 worldNormal = normalize(mul(WorldToObject4x3(), normal).xyz);

    //load material properties
    Material material;
    material = getMaterialProperties(sceneInfo.materialBuffer, sceneInfo.materialByteStride, materialIndex);

    float3 N = normalize(worldNormal);
    float3 V = -WorldRayDirection();

    if (dot(N, V) < 0.0)
        N = -N;

    payload.color = calculateColor(material, N, V, worldPos);
}

[shader("miss")]
void rmissMain(inout HitPayload payload) {
    payload.color = float3(0.1, 0.1, 0.1);
    payload.depth = MISS_DEPTH;
}

[shader("miss")]
void rmissShadowMain(inout ShadowPayload payload) {
    payload.depth = MISS_DEPTH;
}