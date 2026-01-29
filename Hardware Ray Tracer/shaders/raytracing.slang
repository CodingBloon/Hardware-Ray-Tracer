#pragma once
#include "random.slang"
#include "objects.slang"
#include "light.slang"
#include "constants.slang"
#include "disney.slang"
#include "sampler.slang"

struct UniformBuffer {
    float4x4 viewInverse;
    float4x4 projInverse;
    uint32_t frame;
    uint32_t depthMax;
    float LIGHT_THRESHOLD;
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

float3 calculateColor(Material mat, float3 normal, float3 view, float3 worldPos) {
    float3 accumulatedColor = float3(0, 0, 0);
    uint numLights = (uint) sceneInfo.numLights;
    
    //This will later be replaced with a light bounding volume hierarchy system
    for (uint i = 0; i < numLights; i++) {
        Light light = processLight(sceneInfo.lightBuffer, sceneInfo.lightByteStride, i, worldPos); // get light information
        if (light.intensity < LIGHT_TRESHOLD) //lights under this threshold should be ignored because they do not contribute much to the final image
            continue;
        float3 L = normalize(light.direction);
        float3 color = BRDF(&mat, normal, view, L);
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

    float3 c = float3(0.0f);

    for (uint i = 0; i < SAMPLES; i++) {
        bool terminate; // this will use russian roulette to determine if a path should be terminated
        // Paths that missed all objects will be automatically terminated
        while (payload.depth < uniformBuffer.depthMax) {
            float prevWeight = payload.weight;
            TraceRay(topLevelAS, rayFlags, 0xff, 0, 0, 0, ray, payload);
            c += payload.color * prevWeight;

            ray.Direction = payload.rayDirection;
            ray.Origin = payload.rayOrigin;
        }
    }

    c /= SAMPLES;

    float3 color = c;
    outImage[int2(launchID)] = float4(color, 1.0);
}

[shader("closesthit")]
void rchitMain(inout HitPayload payload, in BuiltInTriangleIntersectionAttributes attr) {
    float3 barycentrics = float3(1 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);

    uint instanceID = InstanceIndex();
    uint meshID = InstanceID();
    uint triID = PrimitiveIndex();

    uint64_t indexBuffer = getIndexBufferAddress(meshID, sceneInfo.instanceBuffer, sceneInfo.instanceByteStride);
    uint64_t vertexBuffer = getVertexBufferAddress(meshID, sceneInfo.instanceBuffer, sceneInfo.instanceByteStride);

    int3 indices = getIndices(indexBuffer, triID);

    Triangle tri = getTriangleInformation(vertexBuffer, sceneInfo.vertexByteStride, indices, barycentrics);
    float3 worldPos = float3(mul(float4(tri.pos, 1.0), ObjectToWorld4x3()));
    float3 worldNormal = normalize(mul(WorldToObject4x3(), tri.normal).xyz);

    Material material = getMaterial(sceneInfo.materialBuffer, sceneInfo.materialByteStride, sceneInfo.instanceBuffer, sceneInfo.instanceByteStride, instanceID);

    float3 N = normalize(worldNormal);
    float3 V = WorldRayDirection();

    if (dot(N, -V) < 0.0)
        N = -N;

    float3 color = calculateColor(material, N, -V, worldPos);
    uint seed = uniformBuffer.frame;
    float2 randoms = float2(rand(seed), rand(seed));
    float pdf;
    payload.depth++;
    payload.rayOrigin = worldPos + N * 0.001;
    // payload.rayDirection = sampleGGXVNDFSphericalCap(&material, V, N, randoms, pdf);//sample(&material, N, V, seed, pdf);
    // payload.weight *= material.metallic * pdf;
    payload.weight = 0.0f; //indirect illumination will be deactivated
    payload.color = color;
}

[shader("miss")]
void rmissMain(inout HitPayload payload) {
    payload.color = float3(0.0);
    payload.depth = MISS_DEPTH;
}

[shader("miss")]
void rmissShadowMain(inout ShadowPayload payload) {
    payload.depth = MISS_DEPTH;
}