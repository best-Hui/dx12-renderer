#ifndef RAYTRACING_DEMO_SCENE_LIGHTING_HLSLI
#define RAYTRACING_DEMO_SCENE_LIGHTING_HLSLI

struct SkyLightData
{
    float4 ColorAndIntensity;
};

struct DirectionalLightData
{
    float4 DirectionAndAngularRadius;
    float4 ColorAndIntensity;
};

struct PointLightData
{
    float4 PositionAndRange;
    float4 ColorAndIntensity;
    float4 Attenuation;
};

struct AreaLightData
{
    float4 PositionAndRange;
    float4 NormalAndType;
    float4 AxisUAndExtent;
    float4 AxisVAndExtent;
    float4 ColorAndIntensity;
};

#endif
