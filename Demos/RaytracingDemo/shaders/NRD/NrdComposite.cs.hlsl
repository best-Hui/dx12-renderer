#include <ShaderLibrary/Common/RootSignature.hlsli>

cbuffer NrdCompositeConstants : register(b0)
{
    uint Width;
    uint Height;
    uint Padding0;
    uint Padding1;
};

Texture2D<float4> DenoisedRadiance : register(t0);
Texture2D<float> DepthTexture : register(t1);
RWTexture2D<float4> Output : register(u0);

float3 ToneMap(float3 color)
{
    color = color / (color + 1.0f);
    return pow(saturate(color), 1.0f / 2.2f);
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadId.xy;
    if (pixel.x >= Width || pixel.y >= Height)
    {
        return;
    }

    if (DepthTexture.Load(int3(pixel, 0)) >= 1.0f)
    {
        return;
    }

    float3 color = DenoisedRadiance.Load(int3(pixel, 0)).rgb;
    Output[pixel] = float4(ToneMap(color), 1.0f);
}
