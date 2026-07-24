#include "../GBuffer/GBufferLayout.hlsli"
#include "../Scene/SceneCamera.hlsli"
#include "../../../../External/NRD/Shaders/NRD.hlsli"

Texture2D<float4> GBufferTextures[GBuffer_Count] : register(t0, space0);
Texture2D<float> DepthTexture : register(t5, space0);
TextureCube Skybox : register(t6, space0);
Texture2D<float4> DirectLightingTexture : register(t7, space0);
Texture2D<float4> IndirectLightingTexture : register(t8, space0);
RWTexture2D<float4> Output : register(u0, space0);
RWTexture2D<float4> Accumulation : register(u1, space0);
RWTexture2D<float4> NrdNoisyRadiance : register(u2, space0);
SamplerState LinearWrapSampler : register(s0);

float3 ToneMap(float3 color)
{
    color = color / (color + 1.0f);
    return pow(saturate(color), 1.0f / 2.2f);
}

float3 SanitizeNrdRadiance(float3 color)
{
    if (!all(isfinite(color)))
    {
        return 0.0f;
    }

    return min(max(color, 0.0f), 10000.0f);
}

float3 SampleSkybox(float3 direction)
{
    return Skybox.SampleLevel(LinearWrapSampler, direction, 0.0f).rgb * Camera_SkyLight.ColorAndIntensity.rgb * Camera_SkyLight.ColorAndIntensity.w;
}

float3 GetPrimaryRayDirection(uint2 pixel)
{
    const float2 uv = (float2(pixel) + 0.5f) / float2(Camera_Width, Camera_Height);
    float4 clip = float4(uv * 2.0f - 1.0f, 1.0f, 1.0f);
    clip.y = -clip.y;
    float4 view = mul(clip, Camera_InverseProjection);
    view.xyz /= max(view.w, 0.0001f);
    return normalize(mul(float4(normalize(view.xyz), 0.0f), Camera_InverseView).xyz);
}

float3 GetNrdDiffuseDemodulation(uint2 pixel)
{
    const float4 albedoOcclusion = GBufferTextures[GBuffer_AlbedoOcclusion].Load(int3(pixel, 0));
    const float4 emissionMetallic = GBufferTextures[GBuffer_EmissionMetallic].Load(int3(pixel, 0));
    const float metallic = saturate(emissionMetallic.a);
    const float3 diffuseFactor = max(saturate(albedoOcclusion.rgb) * (1.0f - metallic), 0.05f);
    return lerp(diffuseFactor, float3(1.0f, 1.0f, 1.0f), metallic);
}

float GetGBufferRoughness(uint2 pixel)
{
    const float4 specularSmoothness = GBufferTextures[GBuffer_SpecularSmoothness].Load(int3(pixel, 0));
    return 1.0f - saturate(specularSmoothness.a);
}

float GetGBufferViewZ(uint2 pixel)
{
    const float3 positionWs = GBufferTextures[GBuffer_Position].Load(int3(pixel, 0)).xyz;
    const float3 cameraForward = normalize(mul(float4(0.0f, 0.0f, 1.0f, 0.0f), Camera_InverseView).xyz);
    return max(0.001f, dot(positionWs - Camera_Position.xyz, cameraForward));
}

float4 PackNrdDiffuseRadianceHitDistance(float3 radiance, float hitDistance, float viewZ, float roughness)
{
    radiance = SanitizeNrdRadiance(radiance);
    if (Camera_NrdDenoiserMode == 1u)
    {
        const float3 hitDistanceParams = float3(3.0f, 0.1f, 20.0f);
        const float normHitDistance = REBLUR_FrontEnd_GetNormHitDist(
            max(0.0f, hitDistance),
            viewZ,
            hitDistanceParams,
            max(0.001f, roughness));
        return REBLUR_FrontEnd_PackRadianceAndNormHitDist(radiance, normHitDistance, false);
    }

    return RELAX_FrontEnd_PackRadianceAndHitDist(radiance, max(0.0f, hitDistance), false);
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    if (pixel.x >= Camera_Width || pixel.y >= Camera_Height)
    {
        return;
    }

    const float depth = DepthTexture.Load(int3(pixel, 0));
    if (depth >= 1.0f)
    {
        const float3 skyColor = SampleSkybox(GetPrimaryRayDirection(pixel));
        NrdNoisyRadiance[pixel] = PackNrdDiffuseRadianceHitDistance(skyColor, 0.0f, 1000000.0f, 1.0f);
        return;
    }

    float3 sampleColor = 0.0f;
    float hitDistance = 0.0f;
    if (Camera_DirectLightingEnabled != 0u)
    {
        sampleColor += DirectLightingTexture.Load(int3(pixel, 0)).rgb;
    }
    if (Camera_IndirectLightingEnabled != 0u)
    {
        const float4 indirectLighting = IndirectLightingTexture.Load(int3(pixel, 0));
        sampleColor += indirectLighting.rgb;
        hitDistance = indirectLighting.a;
    }
    const float viewZ = GetGBufferViewZ(pixel);
    const float roughness = GetGBufferRoughness(pixel);
    const float3 nrdDemodulation = GetNrdDiffuseDemodulation(pixel);
    NrdNoisyRadiance[pixel] = PackNrdDiffuseRadianceHitDistance(sampleColor / nrdDemodulation, hitDistance, viewZ, roughness);

    if (Camera_AccumulationEnabled == 0u)
    {
        Output[pixel] = float4(ToneMap(sampleColor), 1.0f);
        return;
    }

    const uint previousSampleCount = Camera_AccumulationFrameIndex;
    float3 accumulatedColor = sampleColor;
    if (previousSampleCount > 0u)
    {
        const float3 history = Accumulation[pixel].rgb;
        accumulatedColor = (history * float(previousSampleCount) + sampleColor) / float(previousSampleCount + 1u);
    }

    Accumulation[pixel] = float4(accumulatedColor, float(previousSampleCount + 1u));
    Output[pixel] = float4(ToneMap(accumulatedColor), 1.0f);
}
