#ifndef RAYTRACING_DEMO_PATH_TRACING_SURFACE_HLSLI
#define RAYTRACING_DEMO_PATH_TRACING_SURFACE_HLSLI

struct SurfaceData
{
    float3 Diffuse;
    float3 Specular;
    float3 PositionWs;
    float3 NormalWs;
    float3 PositionError;
    float Metallic;
    float Roughness;
    float AmbientOcclusion;
    bool Valid;
};

#endif
