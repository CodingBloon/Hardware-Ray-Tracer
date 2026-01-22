#pragma once
#include "material.slang"

struct Triangle {
    float3 pos;
    float3 normal;
    float2 uv;
};

__generic<T> T readBuffer(uint64_t address) {
    T *ptr = (T *)(address);
    return ptr[0];
}

uint64_t getVertexBufferAddress(uint meshID, uint64_t instanceAddress, uint64_t byteStride) {
    uint64_t *ptr = (uint64_t *)(instanceAddress + meshID * byteStride);
    return ptr[0];
}

uint64_t getIndexBufferAddress(uint meshID, uint64_t instanceAddress, uint64_t byteStride) {
    uint64_t *ptr = (uint64_t *)(instanceAddress + meshID * byteStride + 8);
    return ptr[0];
}

uint32_t getMaterialIndex(uint64_t bufferAddress, uint64_t byteStride, uint instance) {
    uint32_t *ptr = (uint32_t *)(bufferAddress + byteStride * instance + 16);
    return ptr[0];
}

int3 getIndices(uint64_t bufferAddress, uint primitiveID) {
    int3 *indices = (int3 *)(bufferAddress);
    return indices[primitiveID];
}

__generic<T : IFloat> T readVertexBuffer(uint64_t bufferAddress, uint64_t byteStride, uint64_t offset, uint3 index, float3 barycentrics) {
    T attr0 = readBuffer<T>(bufferAddress + offset + byteStride * index.x);
    T attr1 = readBuffer<T>(bufferAddress + offset + byteStride * index.y);
    T attr2 = readBuffer<T>(bufferAddress + offset + byteStride * index.z);

    return T(barycentrics.x) * attr0 + T(barycentrics.y) * attr1 + T(barycentrics.z) * attr2;
}

Triangle getTriangleInformation(uint64_t bufferAddress,
                            uint64_t byteStride,
                            uint3 index,
                            float3 barycentrics) {

    Triangle tri;
    tri.pos = readVertexBuffer<float3>(bufferAddress, byteStride, 0, index, barycentrics);
    tri.normal = readVertexBuffer<float3>(bufferAddress, byteStride, 12, index, barycentrics);
    tri.uv = readVertexBuffer<float2>(bufferAddress, byteStride, 24, index, barycentrics);

    return tri;
}

Material getMaterial(uint64_t materialBuf, uint64_t materialStride, uint64_t instanceBuf, uint64_t instanceStride, uint instanceID) {
    uint32_t index = readBuffer<uint32_t>(instanceBuf + instanceStride * instanceID + 16);
    return readBuffer<Material>(materialBuf + materialStride * index);
}