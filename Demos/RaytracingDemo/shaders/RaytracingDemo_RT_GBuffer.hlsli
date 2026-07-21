#ifndef RAYTRACING_DEMO_RT_GBUFFER_HLSLI
#define RAYTRACING_DEMO_RT_GBUFFER_HLSLI

#include "RaytracingDemo_RT_Types.hlsli"

float3 DecodeNormal(float3 encoded)
{
    return normalize(encoded * 2.0f - 1.0f);
}

SurfaceData LoadGBufferSurface(uint2 pixel)
{
    SurfaceData surface;
    surface.Diffuse = 0.0f;
    surface.Specular = 0.0f;
    surface.PositionWs = 0.0f;
    surface.NormalWs = float3(0.0f, 1.0f, 0.0f);
    surface.Metallic = 0.0f;
    surface.Roughness = 1.0f;
    surface.Valid = false;

    float4 diffuse = GBufferTextures[GBuffer_Diffuse].Load(int3(pixel, 0));
    if (diffuse.a <= 0.0f)
    {
        return surface;
    }

    float4 specular = GBufferTextures[GBuffer_Specular].Load(int3(pixel, 0));
    float4 normalRoughness = GBufferTextures[GBuffer_NormalRoughness].Load(int3(pixel, 0));
    float4 positionMetallic = GBufferTextures[GBuffer_PositionMetallic].Load(int3(pixel, 0));

    surface.Diffuse = saturate(diffuse.rgb);
    surface.Specular = saturate(specular.rgb);
    surface.NormalWs = DecodeNormal(normalRoughness.xyz);
    surface.Roughness = saturate(normalRoughness.w);
    surface.PositionWs = positionMetallic.xyz;
    surface.Metallic = saturate(positionMetallic.w);
    surface.Valid = true;
    return surface;
}

#endif
