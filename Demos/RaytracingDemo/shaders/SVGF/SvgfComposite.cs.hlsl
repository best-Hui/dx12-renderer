#include <ShaderLibrary/Common/RootSignature.hlsli>
#include "SvgfCommon.hlsli"

cbuffer SvgfCompositeConstants : register(b0)
{
    uint Width;
    uint Height;
    uint Padding0;
    uint Padding1;
};

Texture2D<float4> FilteredColor : register(t0);
Texture2D<float> DepthTexture : register(t1);
Texture2D<float4> GBufferAlbedoOcclusion : register(t2);
Texture2D<float4> GBufferEmissionMetallic : register(t3);
RWTexture2D<float4> Output : register(u0);

float3 GetSvgfDemodulation(uint2 pixel)
{
    float3 diffuse = saturate(GBufferAlbedoOcclusion.Load(int3(pixel, 0)).rgb);
    float metallic = saturate(GBufferEmissionMetallic.Load(int3(pixel, 0)).a);
    float3 diffuseFactor = max(diffuse * (1.0f - metallic), 0.05f);
    return lerp(diffuseFactor, float3(1.0f, 1.0f, 1.0f), metallic);
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    if (pixel.x >= Width || pixel.y >= Height)
    {
        return;
    }

    if (!SvgfIsValidDepth(DepthTexture.Load(int3(pixel, 0))))
    {
        return;
    }

    const float3 color = FilteredColor.Load(int3(pixel, 0)).rgb * GetSvgfDemodulation(pixel);
    Output[pixel] = float4(SvgfToneMap(color), 1.0f);
}
