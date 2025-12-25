#define PI 3.1415926535897F
#define TWO_PI 6.2831853071795F
#define PI_DIV_2 1.5707963267948F
#define PI_DIV_4 0.7853981633974F

#define MISS_DEPTH 1000
#define INFINITE 1e32F

struct UniformBuffer {
    float4x4 viewInverse;
    float4x4 projInverse;
};

RaytracingAccelerationStructure topLevelAS;
RWTexture2D<float4> outImage;
ConstantBuffer<UniformBuffer> uniformBuffer;
//Add address book to reference buffer by their address
/*
 * There will be a buffer for
 * materials
 * indices
 * lights
 * normals and uvs
 */

struct HitPayload {
    float3 color;
    float weight;
    int depth;
};

__generic<T : IFloat> T getAttribute(uint8_t *bufferAddress, uint32_t offset, uint32_t byteStride, uint attributeIndex) {
    T *ptr = (T *)(bufferAddress + offset + attributeIndex * byteStride);
    return ptr[0];
}

int3 getTriangleIndices(uint8_t* bufferAddress, int primitiveID) {
    // Indices are always 32-bit format
    int3 *indices = (int3 *)(bufferAddress + 0);
    return indices[primitiveID];
}

__generic<T : IFloat> T getTriangleAttribute(uint8_t* bufferAddress, uint32_t offset, uint32_t byteStride, uint3 attributeIndex, float3 barycentrics) {

    T attr0 = getAttribute<T>(bufferAddress, offset, byteStride, attributeIndex.x);
    T attr1 = getAttribute<T>(bufferAddress, offset, byteStride, attributeIndex.y);
    T attr2 = getAttribute<T>(bufferAddress, offset, byteStride, attributeIndex.z);

    return T(barycentrics.x) * attr0 + T(barycentrics.y) * attr1 + T(barycentrics.z) * attr2;
}

float testShadow(float3 worldPos, float3 worldNormal, float3 lightDirection, bool directional) {
    RayDesc shadowRay;
    shadowRay.Origin = worldPos + worldNormal * 0.001;
    shadowRay.Direction = normalize(lightDirection);
    shadowRay.TMin = 0.001;
    shadowRay.TMax = directional ? INFINITE : length(lightDirection);

    HitPayload shadowPayload;
    shadowPayload.depth = 0;

    TraceRay(topLevelAS, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xff, 0, 0, 0, shadowRay, shadowPayload);

    return shadowPayload.depth != MISS_DEPTH ? 0.0 : 1.0;
}

[shader("raygeneration")]
void rgenMain() {
    float2 launchID = (float2)DispatchRaysIndex().xy;
    float2 launchSize = (float2)DispatchRaysDimensions().xy;

    

    const uint rayFlags = 0;

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

    //load additional Information

    int3 indices = getTriangleIndices(nullptr, triID);
    float3 pos = getTriangleAttribute<float3>(nullptr, 0, 0, 0, barycentrics);
    float3 normal = getTriangleAttribute<float3>(nullptr, 0, 0, 0, barycentrics);
    float2 uvWorld = getTriangleAttribute<float2>(nullptr, 0, 0, 0, barycentrics);

    float3 worldPos = float3(mul(float4(pos, 1.0), ObjectToWorld4x3()));
    float3 worldNormal = normalize(mul(WorldToObject4x3(), normal).xyz);

    //load material properties
    float albedo;
    float metallic;
    float roughness;

    float3 lightDirection;
    float3 lightColor;
    float intensity;

    float3 N = normalize(worldNormal);
    float3 V = -WorldRayDirection();
    float3 L = normalize(lightDirection);

    float shadowFactor;

    float3 color = lightColor * intensity * shadowFactor;

    payload.color = float3(1.0, 1.0, 1.0);
}

[shader("miss")]
void rmissMain(inout HitPayload payload) {
    payload.color = float3(0.0, 0.0, 0.2);
    payload.depth = MISS_DEPTH;
}