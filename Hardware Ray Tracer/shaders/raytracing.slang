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

struct HitPayload {
    float3 color;
    float weight;
    int depth;
};

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
    payload.color = float3(1.0, 1.0, 1.0);
}

[shader("miss")]
void rmissMain(inout HitPayload payload) {
    payload.color = float3(0.0, 0.0, 0.2);
    payload.depth = MISS_DEPTH;
}