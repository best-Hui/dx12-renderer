#ifndef RAYTRACING_DEMO_RT_LIGHTING_HLSLI
#define RAYTRACING_DEMO_RT_LIGHTING_HLSLI

#include "RaytracingDemo_RT_Random.hlsli"

float3 SampleSkybox(float3 direction)
{
    return Skybox.SampleLevel(LinearWrapSampler, direction, 0.0f).rgb;
}

RayPayload TraceScene(float3 origin, float3 direction, float tMax, uint flags)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.01f;
    ray.TMax = tMax;

    RayPayload payload;
    payload.BaseColor = 0.0f;
    payload.HitT = 0.0f;
    payload.Normal = 0.0f;
    payload.Hit = 0u;

    TraceRay(Scene, flags, 0xFF, 0, 1, 0, ray, payload);
    return payload;
}

bool IsVisibleToLight(float3 origin, float3 direction, float distanceToLight)
{
    RayPayload shadowPayload = TraceScene(
        origin,
        direction,
        max(0.0f, distanceToLight - 0.03f),
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH);
    return shadowPayload.Hit == 0u;
}

float3 EvaluatePointLight(PathTracingLightData light, float3 positionWs, float3 normalWs, float3 diffuseColor)
{
    float3 toLight = light.PositionAndRange.xyz - positionWs;
    float distanceToLight = length(toLight);
    if (distanceToLight <= 0.001f || distanceToLight > light.PositionAndRange.w)
    {
        return 0.0f;
    }

    float3 lightDirection = toLight / distanceToLight;
    float nDotL = saturate(dot(normalWs, lightDirection));
    if (nDotL <= 0.0f)
    {
        return 0.0f;
    }

    if (!IsVisibleToLight(positionWs + normalWs * 0.03f, lightDirection, distanceToLight))
    {
        return 0.0f;
    }

    float3 attenuationTerms = light.Attenuation.xyz;
    float attenuation = rcp(max(
        0.001f,
        attenuationTerms.x + attenuationTerms.y * distanceToLight + attenuationTerms.z * distanceToLight * distanceToLight));
    float3 radiance = light.ColorAndIntensity.rgb * light.ColorAndIntensity.w * attenuation;
    return diffuseColor * INV_PI * radiance * nDotL;
}

float3 EvaluateDirectLighting(float3 positionWs, float3 normalWs, float3 diffuseColor, inout uint rngState)
{
    uint lightCount = min(Camera_LightCount, MaxPathTracingLights);
    if (lightCount == 0u)
    {
        return 0.0f;
    }

    uint lightIndex = min(uint(Random01(rngState) * float(lightCount)), lightCount - 1u);
    return EvaluatePointLight(Camera_Lights[lightIndex], positionWs, normalWs, diffuseColor) * float(lightCount);
}

float3 TraceGBufferPath(SurfaceData surface, inout uint rngState)
{
    float3 radiance = EvaluateDirectLighting(surface.PositionWs, surface.NormalWs, surface.Diffuse, rngState);
    float3 throughput = surface.Diffuse;
    float3 origin = surface.PositionWs + surface.NormalWs * 0.03f;
    float3 direction = SampleCosineHemisphere(surface.NormalWs, rngState);
    uint bounceCount = min(Camera_MaxBounces, 5u);

    [loop]
    for (uint bounce = 0; bounce < bounceCount; ++bounce)
    {
        RayPayload payload = TraceScene(origin, direction, 10000.0f, RAY_FLAG_NONE);
        if (payload.Hit == 0u)
        {
            radiance += throughput * payload.BaseColor;
            break;
        }

        float3 positionWs = origin + direction * payload.HitT;
        float3 normalWs = payload.Normal;
        float3 diffuseColor = saturate(payload.BaseColor);

        radiance += throughput * EvaluateDirectLighting(positionWs, normalWs, diffuseColor, rngState);

        throughput *= diffuseColor;
        if (max(throughput.r, max(throughput.g, throughput.b)) < 0.005f)
        {
            break;
        }

        direction = SampleCosineHemisphere(normalWs, rngState);
        origin = positionWs + normalWs * 0.03f;
    }

    return radiance;
}

#endif
