#include "constants.slang"

enum LightType : uint8_t {
    POINT = 0,
    SPOT = 1,
    DIRECTIONAL = 2
}

struct Light {
    float3 position;
    float3 color;
    float3 direction;
    float intensity;
    LightType type;
}

__generic<T> T readLightInformation(uint64_t bufferAddress, uint64_t offset, uint64_t byteStride, uint ID) {
    T *ptr = (T *)(bufferAddress + offset + byteStride * ID);
    return ptr[0];
}

Light processLight(uint64_t bufferAddress, uint64_t byteStride, uint ID, float3 worldPos) {
    Light light;
    light.position = readLightInformation<float3>(bufferAddress, 0, byteStride, ID);
    light.color = readLightInformation<float3>(bufferAddress, 12, byteStride, ID);
    light.intensity = readLightInformation<float>(bufferAddress, 24, byteStride, ID);
    light.type = readLightInformation<LightType>(bufferAddress, 28, byteStride, ID);

    if (light.type == LightType.POINT) {
        light.direction = light.position - worldPos;
        float d = length(light.direction);
        light.intensity /= (d * d);
        return light;
    }

    light.direction = float3(0.9, -0.1, 0.0);
    return light;
}