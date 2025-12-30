#define PI 3.1415926535897F
#define TWO_PI 6.2831853071795F
#define PI_DIV_2 1.5707963267948F
#define PI_DIV_4 0.7853981633974F

#define MISS_DEPTH 1000
#define INFINITE 1e32F
#define POINT_LIGHT 0
#define DIR_LIGHT 1

struct UniformBuffer {
    float4x4 viewInverse;
    float4x4 projInverse;
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
};

struct Light {
    float3 pos;
    float3 color;
    float3 direction;
    float intensity;
    uint8_t type;
};

RaytracingAccelerationStructure topLevelAS;
RWTexture2D<float4> outImage;
ConstantBuffer<UniformBuffer> uniformBuffer;
GLSLShaderStorageBuffer<SceneInfo> sceneInfo;

struct HitPayload {
    float3 color;
    float weight;
    int depth;
};

__generic<T : IFloat> T getAttribute(uint64_t bufferAddress, uint64_t offset, uint64_t byteStride, uint attributeIndex) {
    T *ptr = (T *)(bufferAddress + offset + attributeIndex * byteStride);
    return ptr[0];
}

int3 getTriangleIndices(uint64_t bufferAddress, int primitiveID) {
    // Indices are always 32-bit format
    int3 *indices = (int3 *)(bufferAddress);
    return indices[primitiveID];
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

Light processLight(uint64_t bufferAddress, uint64_t byteStride, uint lightID, float3 worldPos) {
    Light light;
    {
        float3 *ptr = (float3 *)(bufferAddress + 0 + byteStride * lightID);
        light.pos = ptr[0];
    }
    {
        float3 *ptr = (float3 *)(bufferAddress + 12 + byteStride * lightID);
        light.color = ptr[0];
    }
    {
        float *ptr = (float *)(bufferAddress + 24 + byteStride * lightID);
        light.intensity = ptr[0];
    }
    {
        uint8_t *ptr = (uint8_t *)(bufferAddress + 28 + byteStride * lightID);
        light.type = ptr[0];
    }

    if (light.type == POINT_LIGHT) {
        light.direction = light.pos - worldPos;
        float d = length(light.direction);
        light.intensity /= (d * d);
        return light;
    }

    return light;
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

float3 pbrMaterial(float3 albedo, float metallic, float roughness, float3 N, float3 V, float3 L) {
    return albedo;
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

    uint64_t indexBuffer = getIndexBuffer(meshID, sceneInfo.instanceBuffer, sceneInfo.instanceByteStride);
    uint64_t vertexBuffer = getVertexBuffer(meshID, sceneInfo.instanceBuffer, sceneInfo.instanceByteStride);

    int materialIndex = 0;
    int3 indices = getTriangleIndices(indexBuffer, triID);

    float3 pos = getTriangleAttribute<float3>(vertexBuffer, 0, sceneInfo.vertexByteStride, indices, barycentrics);
    float3 normal = getTriangleAttribute<float3>(vertexBuffer, 12, sceneInfo.vertexByteStride, indices, barycentrics);
    float2 uvWorld = getTriangleAttribute<float2>(vertexBuffer, 24, sceneInfo.vertexByteStride, indices, barycentrics);

    float3 worldPos = float3(mul(float4(pos, 1.0), ObjectToWorld4x3()));
    float3 worldNormal = normalize(mul(WorldToObject4x3(), normal).xyz);

    //load material properties
    float3 albedo = getAttribute<float3>(sceneInfo.materialBuffer, 0, sceneInfo.materialByteStride, materialIndex);
    float metallic = getAttribute<float>(sceneInfo.materialBuffer, 12, sceneInfo.materialByteStride, materialIndex);
    float roughness = getAttribute<float>(sceneInfo.materialBuffer, 16, sceneInfo.materialByteStride, materialIndex);

    Light light = processLight(sceneInfo.lightBuffer, sceneInfo.lightByteStride, 0, worldPos);

    float3 N = normalize(worldNormal);
    float3 V = -WorldRayDirection();
    float3 L = normalize(light.direction);

    float shadowFactor = testShadow(worldPos, worldNormal, light.direction, false);

    float3 color = pbrMaterial(albedo, metallic, roughness, N, V, L);

    color *= light.color * light.intensity * shadowFactor;

    payload.color = color;
    //payload.color = float3(1.0, 1.0, 1.0);
}

[shader("miss")]
void rmissMain(inout HitPayload payload) {
    payload.color = float3(0.1, 0.1, 0.1);
    payload.depth = MISS_DEPTH;
}