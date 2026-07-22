#ifndef RAYTRACING_DEMO_PATH_TRACING_RAYQUERY_HLSLI
#define RAYTRACING_DEMO_PATH_TRACING_RAYQUERY_HLSLI

#define RAYTRACING_DEMO_INLINE_BACKEND 1

#include "PathTracingGeometry.hlsli"

RayPayload TraceScene(float3 origin, float3 direction, float tMax, uint flags)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.0f;
    ray.TMax = tMax;

    RayQuery<RAY_FLAG_NONE> query;
    query.TraceRayInline(RAYTRACING_DEMO_SCENE, flags, 0xFF, ray);
    while (query.Proceed())
    {
    }

    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
    {
        return MakeMissPayload(direction);
    }

    GeometryData geometry = Geometries[query.CommittedInstanceID()];
    MaterialData material = Materials[geometry.MaterialIndex];
    return MakeTrianglePayload(
        geometry,
        material,
        query.CommittedPrimitiveIndex(),
        query.CommittedTriangleBarycentrics(),
        query.WorldRayDirection(),
        query.CommittedObjectToWorld3x4(),
        query.CommittedRayT());
}

#endif
