static const float PI = 3.14159265359f;
static const float INV_PI = 0.31830988618f;
static const uint MaxPathTracingLights = 8;

struct VertexAttributes
{
    float4 Position;
    float4 Normal;
    float4 Uv;
    float4 Tangent;
    float4 Bitangent;
};

struct MaterialData
{
    float4 Diffuse;
    float4 TilingOffset;
    uint DiffuseTextureIndex;
    uint Padding0;
    uint Padding1;
    uint Padding2;
};

struct GeometryData
{
    uint VertexBufferIndex;
    uint IndexBufferIndex;
    uint MaterialIndex;
    uint Padding;
};

struct PathTracingLightData
{
    float4 PositionAndRange;
    float4 ColorAndIntensity;
    float4 Attenuation;
};

struct RayPayload
{
    float3 BaseColor;
    float HitT;
    float3 Normal;
    uint Hit;
};

RaytracingAccelerationStructure Scene : register(t0, space0);
StructuredBuffer<MaterialData> Materials : register(t1, space0);
StructuredBuffer<GeometryData> Geometries : register(t2, space0);

StructuredBuffer<VertexAttributes> VertexBuffers[] : register(t0, space1);
Buffer<uint> IndexBuffers[] : register(t0, space2);
Texture2D<float4> Textures[] : register(t0, space3);

RWTexture2D<float4> Output : register(u0, space0);
SamplerState LinearWrapSampler : register(s0);

cbuffer CameraConstants : register(b0, space0)
{
    matrix Camera_InverseView;
    matrix Camera_InverseProjection;
    float4 Camera_Position;
    uint Camera_Width;
    uint Camera_Height;
    uint Camera_MaxBounces;
    uint Camera_SamplesPerPixel;
    uint Camera_LightCount;
    uint Camera_FrameIndex;
    float2 Camera_TaaJitterOffset;
    PathTracingLightData Camera_Lights[MaxPathTracingLights];
};

uint LoadIndex(uint indexBufferIndex, uint indexNumber)
{
    return IndexBuffers[indexBufferIndex][indexNumber];
}

uint Hash(uint value)
{
    value ^= value >> 17;
    value *= 0xed5ad4bbu;
    value ^= value >> 11;
    value *= 0xac4c1b51u;
    value ^= value >> 15;
    value *= 0x31848babu;
    value ^= value >> 14;
    return value;
}

float Random01(inout uint state)
{
    state = state * 1664525u + 1013904223u;
    return float(state & 0x00ffffffu) / 16777216.0f;
}

float3 ComputeCameraRayDirection(uint2 pixel, uint2 dimensions, float2 jitter)
{
    float2 uv = (float2(pixel) + jitter) / float2(dimensions);
    float2 ndc = uv * 2.0f - 1.0f;
    ndc += Camera_TaaJitterOffset;
    ndc.y = -ndc.y;

    float4 targetVs = mul(Camera_InverseProjection, float4(ndc, 1.0f, 1.0f));
    targetVs.xyz /= targetVs.w;

    float3 directionVs = normalize(targetVs.xyz);
    return normalize(mul(Camera_InverseView, float4(directionVs, 0.0f)).xyz);
}

float3 SampleCosineHemisphere(float3 normal, inout uint rngState)
{
    float u0 = Random01(rngState);
    float u1 = Random01(rngState);

    float r = sqrt(u0);
    float phi = 2.0f * PI * u1;
    float x = r * cos(phi);
    float y = r * sin(phi);
    float z = sqrt(max(0.0f, 1.0f - u0));

    float3 up = abs(normal.z) < 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 tangent = normalize(cross(up, normal));
    float3 bitangent = cross(normal, tangent);
    return normalize(tangent * x + bitangent * y + normal * z);
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

float3 EvaluatePointLight(PathTracingLightData light, float3 positionWs, float3 normalWs, float3 baseColor)
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
    return baseColor * INV_PI * radiance * nDotL;
}

float3 EvaluateDirectLighting(float3 positionWs, float3 normalWs, float3 baseColor, inout uint rngState)
{
    uint lightCount = min(Camera_LightCount, MaxPathTracingLights);
    if (lightCount == 0u)
    {
        return 0.0f;
    }

    uint lightIndex = min(uint(Random01(rngState) * float(lightCount)), lightCount - 1u);
    return EvaluatePointLight(Camera_Lights[lightIndex], positionWs, normalWs, baseColor) * float(lightCount);
}

float3 TracePath(float3 origin, float3 direction, inout uint rngState)
{
    float3 radiance = 0.0f;
    float3 throughput = 1.0f;
    uint bounceCount = min(Camera_MaxBounces, 5u);

    [loop]
    for (uint bounce = 0; bounce < bounceCount; ++bounce)
    {
        RayPayload payload = TraceScene(origin, direction, 10000.0f, RAY_FLAG_NONE);
        if (payload.Hit == 0u)
        {
            break;
        }

        float3 positionWs = origin + direction * payload.HitT;
        float3 normalWs = payload.Normal;
        float3 baseColor = saturate(payload.BaseColor);

        radiance += throughput * EvaluateDirectLighting(positionWs, normalWs, baseColor, rngState);

        throughput *= baseColor;
        if (max(throughput.r, max(throughput.g, throughput.b)) < 0.005f)
        {
            break;
        }

        direction = SampleCosineHemisphere(normalWs, rngState);
        origin = positionWs + normalWs * 0.03f;
    }

    return radiance;
}

float3 ToneMap(float3 color)
{
    color = color / (color + 1.0f);
    return pow(saturate(color), 1.0f / 2.2f);
}

[shader("raygeneration")]
void RayGen()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 dimensions = DispatchRaysDimensions().xy;
    uint samplesPerPixel = max(1u, Camera_SamplesPerPixel);

    uint rngState = Hash(pixel.x + pixel.y * dimensions.x + Camera_FrameIndex * 9781u);
    float3 color = 0.0f;

    [loop]
    for (uint sampleIndex = 0; sampleIndex < samplesPerPixel; ++sampleIndex)
    {
        float2 jitter = float2(Random01(rngState), Random01(rngState));
        float3 direction = ComputeCameraRayDirection(pixel, dimensions, jitter);
        color += TracePath(Camera_Position.xyz, direction, rngState);
    }

    color /= float(samplesPerPixel);
    Output[pixel] = float4(ToneMap(color), 1.0f);
}

[shader("miss")]
void Miss(inout RayPayload payload)
{
    payload.Hit = 0u;
    payload.HitT = 0.0f;
    payload.BaseColor = 0.0f;
    payload.Normal = 0.0f;
}

[shader("closesthit")]
void ClosestHit(inout RayPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    GeometryData geometry = Geometries[InstanceID()];
    MaterialData material = Materials[geometry.MaterialIndex];

    uint firstIndex = PrimitiveIndex() * 3;
    uint i0 = LoadIndex(geometry.IndexBufferIndex, firstIndex + 0);
    uint i1 = LoadIndex(geometry.IndexBufferIndex, firstIndex + 1);
    uint i2 = LoadIndex(geometry.IndexBufferIndex, firstIndex + 2);

    VertexAttributes v0 = VertexBuffers[geometry.VertexBufferIndex][i0];
    VertexAttributes v1 = VertexBuffers[geometry.VertexBufferIndex][i1];
    VertexAttributes v2 = VertexBuffers[geometry.VertexBufferIndex][i2];

    float3 barycentrics = float3(
        1.0f - attributes.barycentrics.x - attributes.barycentrics.y,
        attributes.barycentrics.x,
        attributes.barycentrics.y);

    float2 uv = v0.Uv.xy * barycentrics.x + v1.Uv.xy * barycentrics.y + v2.Uv.xy * barycentrics.z;
    uv = uv * material.TilingOffset.xy + material.TilingOffset.zw;

    float3 p0Ws = mul(ObjectToWorld3x4(), float4(v0.Position.xyz, 1.0f));
    float3 p1Ws = mul(ObjectToWorld3x4(), float4(v1.Position.xyz, 1.0f));
    float3 p2Ws = mul(ObjectToWorld3x4(), float4(v2.Position.xyz, 1.0f));
    float3 normalWs = normalize(cross(p1Ws - p0Ws, p2Ws - p0Ws));
    normalWs = dot(normalWs, WorldRayDirection()) > 0.0f ? -normalWs : normalWs;

    float4 texel = Textures[material.DiffuseTextureIndex].SampleLevel(LinearWrapSampler, uv, 0.0f);

    payload.Hit = 1u;
    payload.HitT = RayTCurrent();
    payload.Normal = normalWs;
    payload.BaseColor = material.Diffuse.rgb * texel.rgb;
}
