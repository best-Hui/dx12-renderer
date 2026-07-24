#include <ShaderLibrary/Common/RootSignature.hlsli>
#include "../../../../External/NRD/Shaders/NRD.hlsli"

cbuffer NrdPrepareConstants : register(b0)
{
    row_major matrix WorldToView;
    uint Width;
    uint Height;
    uint Padding0;
    uint Padding1;
};

Texture2D<float4> GBufferSpecularSmoothness : register(t0);
Texture2D<float4> GBufferNormal : register(t1);
Texture2D<float4> GBufferPosition : register(t2);
Texture2D<float> DepthTexture : register(t3);
Texture2D<float2> MotionVector : register(t4);

RWTexture2D<float4> NrdNormalRoughness : register(u0);
RWTexture2D<float> NrdViewZ : register(u1);
RWTexture2D<float2> NrdMotion : register(u2);

float3 DecodeDemoNormal(float3 encoded)
{
    return normalize(encoded * 2.0f - 1.0f);
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadId.xy;
    if (pixel.x >= Width || pixel.y >= Height)
    {
        return;
    }

    float depth = DepthTexture.Load(int3(pixel, 0));
    if (depth >= 1.0f)
    {
        NrdNormalRoughness[pixel] = NRD_FrontEnd_PackNormalAndRoughness(float3(0.0f, 0.0f, 1.0f), 1.0f, 0.0f);
        NrdViewZ[pixel] = 1000000.0f;
        NrdMotion[pixel] = 0.0f;
        return;
    }

    float4 normalSample = GBufferNormal.Load(int3(pixel, 0));
    float4 specularSmoothness = GBufferSpecularSmoothness.Load(int3(pixel, 0));
    float4 position = GBufferPosition.Load(int3(pixel, 0));

    float3 normalWs = DecodeDemoNormal(normalSample.xyz);
    float roughness = 1.0f - saturate(specularSmoothness.a);
    float viewZ = mul(float4(position.xyz, 1.0f), WorldToView).z;

    NrdNormalRoughness[pixel] = NRD_FrontEnd_PackNormalAndRoughness(normalWs, roughness, 0.0f);
    NrdViewZ[pixel] = viewZ;
    NrdMotion[pixel] = MotionVector.Load(int3(pixel, 0));
}
