#ifndef RAYTRACING_DEMO_RT_RANDOM_HLSLI
#define RAYTRACING_DEMO_RT_RANDOM_HLSLI

#include "RaytracingDemo_RT_Types.hlsli"

uint Hash(uint value)
{
    value ^= value >> 17;
    value *= 0xed5ad4bbu;
    value ^= value >> 11;
    value *= 0xac4c1b51u;
    value ^= value >> 15;
    value *= 0x31848babu;
    value ^= value >> 14;
    return value;
}

float Random01(inout uint state)
{
    state = state * 1664525u + 1013904223u;
    return float(state & 0x00ffffffu) / 16777216.0f;
}

float3 SampleCosineHemisphere(float3 normal, inout uint rngState)
{
    float u0 = Random01(rngState);
    float u1 = Random01(rngState);

    float r = sqrt(u0);
    float phi = 2.0f * PI * u1;
    float x = r * cos(phi);
    float y = r * sin(phi);
    float z = sqrt(max(0.0f, 1.0f - u0));

    float3 up = abs(normal.z) < 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 tangent = normalize(cross(up, normal));
    float3 bitangent = cross(normal, tangent);
    return normalize(tangent * x + bitangent * y + normal * z);
}

#endif
