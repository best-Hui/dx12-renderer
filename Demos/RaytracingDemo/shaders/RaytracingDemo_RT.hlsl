#define RAYTRACING_DEMO_ENABLE_LINALG 0

#if defined(RAYTRACING_DEMO_ENABLE_LINALG) && RAYTRACING_DEMO_ENABLE_LINALG
#include <hlsl/dx/linalg.h>
#endif
#include "RaytracingDemo_RT_GBuffer.hlsli"
#include "RaytracingDemo_RT_Lighting.hlsli"

uint LoadIndex(uint indexBufferIndex, uint indexNumber)
{
    return IndexBuffers[indexBufferIndex][indexNumber];
}

float3 ToneMap(float3 color)
{
    color = color / (color + 1.0f);
    return pow(saturate(color), 1.0f / 2.2f);
}

float3 ApplyClosestHitCooperativeVector(float3 color)
{
#if defined(RAYTRACING_DEMO_ENABLE_LINALG) && RAYTRACING_DEMO_ENABLE_LINALG
    uint3 quantizedColor = uint3(saturate(color) * 1024.0f + 0.5f);
    dx::linalg::InterpretedVector<float, 3, dx::linalg::ComponentType::F32> convertedColor =
        dx::linalg::Convert<dx::linalg::ComponentType::F32, dx::linalg::ComponentType::U32>(quantizedColor);
    return saturate(float3(convertedColor.Data[0], convertedColor.Data[1], convertedColor.Data[2]) / 1024.0f);
#else
    return color;
#endif
}

[shader("raygeneration")]
void RayGen()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 dimensions = DispatchRaysDimensions().xy;

    SurfaceData surface = LoadGBufferSurface(pixel);
    if (!surface.Valid)
    {
        return;
    }

    uint rngState = Hash(pixel.x + pixel.y * dimensions.x + Camera_FrameIndex * 9781u);
    float3 sampleColor = TraceGBufferPath(surface, rngState);

    uint previousSampleCount = Camera_AccumulationFrameIndex;
    float3 accumulatedColor = sampleColor;
    if (previousSampleCount > 0u)
    {
        float3 history = Accumulation[pixel].rgb;
        accumulatedColor = (history * float(previousSampleCount) + sampleColor) / float(previousSampleCount + 1u);
    }

    Accumulation[pixel] = float4(accumulatedColor, float(previousSampleCount + 1u));
    Output[pixel] = float4(ToneMap(accumulatedColor), 1.0f);
}

[shader("miss")]
void Miss(inout RayPayload payload)
{
    payload.Hit = 0u;
    payload.HitT = 0.0f;
    payload.BaseColor = SampleSkybox(WorldRayDirection());
    payload.Normal = 0.0f;
}

[shader("closesthit")]
void ClosestHit(inout RayPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    GeometryData geometry = Geometries[InstanceID()];
    MaterialData material = Materials[geometry.MaterialIndex];

    uint firstIndex = PrimitiveIndex() * 3;
    uint i0 = LoadIndex(geometry.IndexBufferIndex, firstIndex + 0);
    uint i1 = LoadIndex(geometry.IndexBufferIndex, firstIndex + 1);
    uint i2 = LoadIndex(geometry.IndexBufferIndex, firstIndex + 2);

    VertexAttributes v0 = VertexBuffers[geometry.VertexBufferIndex][i0];
    VertexAttributes v1 = VertexBuffers[geometry.VertexBufferIndex][i1];
    VertexAttributes v2 = VertexBuffers[geometry.VertexBufferIndex][i2];

    float3 barycentrics = float3(
        1.0f - attributes.barycentrics.x - attributes.barycentrics.y,
        attributes.barycentrics.x,
        attributes.barycentrics.y);

    float2 uv = v0.Uv.xy * barycentrics.x + v1.Uv.xy * barycentrics.y + v2.Uv.xy * barycentrics.z;
    uv = uv * material.TilingOffset.xy + material.TilingOffset.zw;

    float3 p0Ws = mul(ObjectToWorld3x4(), float4(v0.Position.xyz, 1.0f));
    float3 p1Ws = mul(ObjectToWorld3x4(), float4(v1.Position.xyz, 1.0f));
    float3 p2Ws = mul(ObjectToWorld3x4(), float4(v2.Position.xyz, 1.0f));
    float3 normalWs = normalize(cross(p1Ws - p0Ws, p2Ws - p0Ws));
    normalWs = dot(normalWs, WorldRayDirection()) > 0.0f ? -normalWs : normalWs;

    float4 texel = Textures[material.DiffuseTextureIndex].SampleLevel(LinearWrapSampler, uv, 0.0f);

    payload.Hit = 1u;
    payload.HitT = RayTCurrent();
    payload.Normal = normalWs;
    payload.BaseColor = ApplyClosestHitCooperativeVector(material.Diffuse.rgb * texel.rgb);
}
