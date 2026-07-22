#include <ShaderLibrary/Common/RootSignature.hlsli>
#include "SvgfCommon.hlsli"

cbuffer SvgfTemporalConstants : register(b0)
{
    uint Width;
    uint Height;
    uint ResetHistory;
    uint Padding0;
    float TemporalAlpha;
    float MomentsAlpha;
    float PhiNormal;
    float PhiDepth;
};

Texture2D<float4> NoisyRadiance : register(t0);
Texture2D<float4> GBufferNormal : register(t1);
Texture2D<float4> GBufferPosition : register(t2);
Texture2D<float> DepthTexture : register(t3);
Texture2D<float4> HistoryColor : register(t4);
Texture2D<float2> HistoryMoments : register(t5);

RWTexture2D<float4> TemporalColor : register(u0);
RWTexture2D<float2> TemporalMoments : register(u1);
RWTexture2D<float> Variance : register(u2);
RWTexture2D<float4> OutHistoryColor : register(u3);
RWTexture2D<float2> OutHistoryMoments : register(u4);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    if (pixel.x >= Width || pixel.y >= Height)
    {
        return;
    }

    const float depth = DepthTexture.Load(int3(pixel, 0));
    const float4 noisy = NoisyRadiance.Load(int3(pixel, 0));
    const float3 noisyColor = max(noisy.rgb, 0.0f);
    const float luminance = SvgfLuminance(noisyColor);
    const float2 currentMoments = float2(luminance, luminance * luminance);

    if (!SvgfIsValidDepth(depth))
    {
        TemporalColor[pixel] = float4(noisyColor, 1.0f);
        TemporalMoments[pixel] = currentMoments;
        Variance[pixel] = 0.0f;
        OutHistoryColor[pixel] = float4(noisyColor, 1.0f);
        OutHistoryMoments[pixel] = currentMoments;
        return;
    }

    float historyWeight = 0.0f;
    if (ResetHistory == 0u)
    {
        const float4 previousColor = HistoryColor.Load(int3(pixel, 0));
        const float2 previousMoments = HistoryMoments.Load(int3(pixel, 0));
        const float3 normal = DecodeSvgfNormal(GBufferNormal.Load(int3(pixel, 0)).xyz);
        const float3 position = GBufferPosition.Load(int3(pixel, 0)).xyz;
        const float normalWeight = SvgfNormalWeight(normal, normal, PhiNormal);
        const float depthWeight = SvgfDepthWeight(depth, depth, position, position, PhiDepth);
        historyWeight = saturate(previousColor.a > 0.0f ? normalWeight * depthWeight : 0.0f);

        const float alpha = lerp(1.0f, TemporalAlpha, historyWeight);
        const float momentsAlpha = lerp(1.0f, MomentsAlpha, historyWeight);
        const float3 accumulatedColor = lerp(previousColor.rgb, noisyColor, alpha);
        const float2 accumulatedMoments = lerp(previousMoments, currentMoments, momentsAlpha);
        const float variance = max(0.0f, accumulatedMoments.y - accumulatedMoments.x * accumulatedMoments.x);
        const float nextHistoryLength = min(previousColor.a + 1.0f, 255.0f);

        TemporalColor[pixel] = float4(accumulatedColor, nextHistoryLength);
        TemporalMoments[pixel] = accumulatedMoments;
        Variance[pixel] = variance;
        OutHistoryColor[pixel] = float4(accumulatedColor, nextHistoryLength);
        OutHistoryMoments[pixel] = accumulatedMoments;
        return;
    }

    TemporalColor[pixel] = float4(noisyColor, 1.0f);
    TemporalMoments[pixel] = currentMoments;
    Variance[pixel] = 0.0f;
    OutHistoryColor[pixel] = float4(noisyColor, 1.0f);
    OutHistoryMoments[pixel] = currentMoments;
}
