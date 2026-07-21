#include <ShaderLibrary/Common/RootSignature.hlsli>
#include <ShaderLibrary/RayTracing/InlineRayTracing.hlsli>

static const float PI = 3.14159265359f;
static const float INV_PI = 0.31830988618f;
static const uint MaxPathTracingLights = 8;
static const uint MaxInlineRayTracingTextures = 8;
static const uint MaxInlineRayTracingGeometryBuffers = 256;

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
    float4 Specular;
    float4 TilingOffset;
    uint DiffuseTextureIndex;
    float Metallic;
    float Roughness;
    uint Padding0;
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

struct SurfaceData
{
    float3 Diffuse;
    float3 Specular;
    float3 PositionWs;
    float3 NormalWs;
    float Metallic;
    float Roughness;
    bool Valid;
};

struct RayHit
{
    float3 BaseColor;
    float3 NormalWs;
    float HitT;
    bool Hit;
};

Texture2D<float4> GBufferBaseColor : register(t0, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
Texture2D<float4> GBufferSpecular : register(t1, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
Texture2D<float4> GBufferNormalRoughness : register(t2, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
Texture2D<float4> GBufferPositionMetallic : register(t3, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
TextureCube Skybox : register(t4, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
StructuredBuffer<MaterialData> Materials : register(t5, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
StructuredBuffer<GeometryData> Geometries : register(t6, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
Texture2D<float4> MaterialTextures[MaxInlineRayTracingTextures] : register(t7, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
StructuredBuffer<VertexAttributes> VertexBuffers[MaxInlineRayTracingGeometryBuffers] : register(t15, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
Buffer<uint> IndexBuffers[MaxInlineRayTracingGeometryBuffers] : register(t271, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);

RWTexture2D<float4> Output : register(u0);
RWTexture2D<float4> Accumulation : register(u1);

SamplerState LinearWrapSampler : register(s1);

cbuffer CameraConstants : register(b0)
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
    uint Camera_AccumulationFrameIndex;
    uint Camera_Padding;
    PathTracingLightData Camera_Lights[MaxPathTracingLights];
};

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

float3 ToneMap(float3 color)
{
    color = color / (color + 1.0f);
    return pow(saturate(color), 1.0f / 2.2f);
}

SurfaceData LoadGBufferSurface(uint2 pixel)
{
    SurfaceData surface;
    surface.Diffuse = 0.0f;
    surface.Specular = 0.0f;
    surface.PositionWs = 0.0f;
    surface.NormalWs = 0.0f;
    surface.Metallic = 0.0f;
    surface.Roughness = 1.0f;
    surface.Valid = false;

    float4 baseColor = GBufferBaseColor.Load(int3(pixel, 0));
    float4 specular = GBufferSpecular.Load(int3(pixel, 0));
    float4 normalRoughness = GBufferNormalRoughness.Load(int3(pixel, 0));
    float4 positionMetallic = GBufferPositionMetallic.Load(int3(pixel, 0));

    if (normalRoughness.a <= 0.0f)
    {
        return surface;
    }

    surface.Diffuse = baseColor.rgb;
    surface.Specular = specular.rgb;
    surface.NormalWs = normalize(normalRoughness.xyz * 2.0f - 1.0f);
    surface.Roughness = saturate(normalRoughness.a);
    surface.PositionWs = positionMetallic.xyz;
    surface.Metallic = saturate(positionMetallic.a);
    surface.Valid = true;
    return surface;
}

uint LoadIndex(uint indexBufferIndex, uint indexNumber)
{
    return IndexBuffers[indexBufferIndex][indexNumber];
}

RayHit ShadeCommittedTriangle(RayQuery<RAY_FLAG_NONE> query)
{
    RayHit hit;
    hit.BaseColor = 0.0f;
    hit.NormalWs = 0.0f;
    hit.HitT = 0.0f;
    hit.Hit = false;

    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
    {
        return hit;
    }

    GeometryData geometry = Geometries[query.CommittedInstanceID()];
    MaterialData material = Materials[geometry.MaterialIndex];

    uint firstIndex = query.CommittedPrimitiveIndex() * 3;
    uint i0 = LoadIndex(geometry.IndexBufferIndex, firstIndex + 0);
    uint i1 = LoadIndex(geometry.IndexBufferIndex, firstIndex + 1);
    uint i2 = LoadIndex(geometry.IndexBufferIndex, firstIndex + 2);

    VertexAttributes v0 = VertexBuffers[geometry.VertexBufferIndex][i0];
    VertexAttributes v1 = VertexBuffers[geometry.VertexBufferIndex][i1];
    VertexAttributes v2 = VertexBuffers[geometry.VertexBufferIndex][i2];

    float2 hitBarycentrics = query.CommittedTriangleBarycentrics();
    float3 barycentrics = float3(
        1.0f - hitBarycentrics.x - hitBarycentrics.y,
        hitBarycentrics.x,
        hitBarycentrics.y);

    float2 uv = v0.Uv.xy * barycentrics.x + v1.Uv.xy * barycentrics.y + v2.Uv.xy * barycentrics.z;
    uv = uv * material.TilingOffset.xy + material.TilingOffset.zw;

    float3 p0Ws = mul(query.CommittedObjectToWorld3x4(), float4(v0.Position.xyz, 1.0f));
    float3 p1Ws = mul(query.CommittedObjectToWorld3x4(), float4(v1.Position.xyz, 1.0f));
    float3 p2Ws = mul(query.CommittedObjectToWorld3x4(), float4(v2.Position.xyz, 1.0f));
    float3 normalWs = normalize(cross(p1Ws - p0Ws, p2Ws - p0Ws));
    normalWs = dot(normalWs, query.WorldRayDirection()) > 0.0f ? -normalWs : normalWs;

    uint textureIndex = min(material.DiffuseTextureIndex, MaxInlineRayTracingTextures - 1u);
    float4 texel = MaterialTextures[textureIndex].SampleLevel(LinearWrapSampler, uv, 0.0f);

    hit.Hit = true;
    hit.HitT = query.CommittedRayT();
    hit.NormalWs = normalWs;
    hit.BaseColor = material.Diffuse.rgb * texel.rgb;
    return hit;
}

RayHit TraceScene(float3 origin, float3 direction, float tMax, uint flags)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.01f;
    ray.TMax = tMax;

    RayQuery<RAY_FLAG_NONE> query;
    query.TraceRayInline(g_InlineRayTracingScene, flags, 0xFF, ray);
    while (query.Proceed())
    {
    }

    return ShadeCommittedTriangle(query);
}

bool IsVisibleToLight(float3 origin, float3 direction, float distanceToLight)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.01f;
    ray.TMax = max(0.0f, distanceToLight - 0.03f);

    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> query;
    query.TraceRayInline(
        g_InlineRayTracingScene,
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
        0xFF,
        ray);

    while (query.Proceed())
    {
    }

    return query.CommittedStatus() == COMMITTED_NOTHING;
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
        RayHit hit = TraceScene(origin, direction, 10000.0f, RAY_FLAG_NONE);
        if (!hit.Hit)
        {
            radiance += throughput * Skybox.SampleLevel(LinearWrapSampler, direction, 0.0f).rgb;
            break;
        }

        float3 positionWs = origin + direction * hit.HitT;
        float3 normalWs = hit.NormalWs;
        float3 diffuseColor = saturate(hit.BaseColor);

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

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadId.xy;
    if (pixel.x >= Camera_Width || pixel.y >= Camera_Height)
    {
        return;
    }

    SurfaceData surface = LoadGBufferSurface(pixel);
    if (!surface.Valid)
    {
        return;
    }

    uint rngState = Hash(pixel.x + pixel.y * Camera_Width + Camera_FrameIndex * 9781u);
    float3 sampleColor = TraceGBufferPath(surface, rngState);

    uint previousSampleCount = Camera_AccumulationFrameIndex;
    float3 accumulatedColor = sampleColor;
    if (previousSampleCount > 0u)
    {
        float3 history = Accumulation[pixel].rgb;
        accumulatedColor = (history * float(previousSampleCount) + sampleColor) / float(previousSampleCount + 1u);
    }

    Accumulation[pixel] = float4(accumulatedColor, float(previousSampleCount + 1u));
    Output[pixel] = float4(ToneMap(accumulatedColor), 1.0f);
}
