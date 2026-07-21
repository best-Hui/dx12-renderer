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

struct RayPayload
{
    float4 Color;
};

RaytracingAccelerationStructure Scene : register(t0, space0);
StructuredBuffer<MaterialData> Materials : register(t1, space0);
StructuredBuffer<GeometryData> Geometries : register(t2, space0);

StructuredBuffer<VertexAttributes> VertexBuffers[] : register(t0, space1);
ByteAddressBuffer IndexBuffers[] : register(t0, space2);
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
    uint Camera_Padding0;
    uint Camera_Padding1;
};

uint LoadIndex(uint indexBufferIndex, uint indexNumber)
{
    uint byteAddress = indexNumber * 2;
    uint packed = IndexBuffers[indexBufferIndex].Load(byteAddress & ~3u);
    return ((byteAddress & 2u) == 0u) ? (packed & 0xFFFFu) : (packed >> 16u);
}

float3 ComputeCameraRayDirection(uint2 pixel, uint2 dimensions)
{
    float2 uv = (float2(pixel) + 0.5f) / float2(dimensions);
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y;

    float4 targetVs = mul(Camera_InverseProjection, float4(ndc, 1.0f, 1.0f));
    targetVs.xyz /= targetVs.w;

    float3 directionVs = normalize(targetVs.xyz);
    return normalize(mul(Camera_InverseView, float4(directionVs, 0.0f)).xyz);
}

[shader("raygeneration")]
void RayGen()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 dimensions = DispatchRaysDimensions().xy;

    RayDesc ray;
    ray.Origin = Camera_Position.xyz;
    ray.Direction = ComputeCameraRayDirection(pixel, dimensions);
    ray.TMin = 0.001f;
    ray.TMax = 10000.0f;

    RayPayload payload;
    payload.Color = float4(0.0f, 0.0f, 0.0f, 1.0f);

    TraceRay(Scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xFF, 0, 1, 0, ray, payload);
    Output[pixel] = payload.Color;
}

[shader("miss")]
void Miss(inout RayPayload payload)
{
    float3 dir = normalize(WorldRayDirection());
    float horizon = saturate(dir.y * 0.5f + 0.5f);
    payload.Color = float4(lerp(float3(0.08f, 0.10f, 0.12f), float3(0.35f, 0.48f, 0.70f), horizon), 1.0f);
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

    float4 texel = Textures[material.DiffuseTextureIndex].SampleLevel(LinearWrapSampler, uv, 0.0f);
    float3 baseColor = material.Diffuse.rgb * texel.rgb;

    float distanceShade = saturate(1.0f - RayTCurrent() / 120.0f);
    float shade = 0.18f + distanceShade * 0.82f;
    payload.Color = float4(baseColor * shade, 1.0f);
}
