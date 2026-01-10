struct Material {
    float3 color;
    float metallic;
    float roughness;

    float3 emissionColor;
    float emissionStrength;
};

__generic<T : IFloat> T readBuffer(uint64_t address) {
    T *ptr = (T *)(address);
    return ptr[0];
}

__generic<T : IFloat> T readVertexBuffer(uint64_t bufferAddress, uint64_t byteStride, uint64_t offset, uint3 index, float3 barycentrics) {
    T attr0 = readBuffer<T>(bufferAddress + offset + byteStride * index.x);
    T attr1 = readBuffer<T>(bufferAddress + offset + byteStride * index.y);
    T attr2 = readBuffer<T>(bufferAddress + offset + byteStride * index.z);

    return T(barycentrics.x) * attr0 + T(barycentrics.y) * attr1 + T(barycentrics.z) * attr2;
}

Material getMaterialProperties(uint64_t bufferAddress, uint64_t byteStride, uint index) {
    Material *ptr = (Material *)(bufferAddress + byteStride * index);
    return ptr[0];
}

void getTriangleInformation(uint64_t bufferAddress,
                            uint64_t byteStride,
                            uint3 index,
                            float3 barycentrics,
                            out float3 pos,
                            out float3 normal,
                            out float2 uv) {
    pos = readVertexBuffer<float3>(bufferAddress, byteStride, 0, index, barycentrics);
    normal = readVertexBuffer<float3>(bufferAddress, byteStride, 12, index, barycentrics);
    uv = readVertexBuffer<float2>(bufferAddress, byteStride, 24, index, barycentrics);
}