#ifndef RAYTRACING_DEMO_RT_TYPES_HLSLI
#define RAYTRACING_DEMO_RT_TYPES_HLSLI

static const float PI = 3.14159265359f;
static const float INV_PI = 0.31830988618f;
static const uint MaxPathTracingLights = 8;

static const uint GBuffer_Diffuse = 0;
static const uint GBuffer_Specular = 1;
static const uint GBuffer_NormalRoughness = 2;
static const uint GBuffer_PositionMetallic = 3;

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

struct RayPayload
{
    float3 BaseColor;
    float HitT;
    float3 Normal;
    uint Hit;
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

RaytracingAccelerationStructure Scene : register(t0, space0);
StructuredBuffer<MaterialData> Materials : register(t1, space0);
StructuredBuffer<GeometryData> Geometries : register(t2, space0);

StructuredBuffer<VertexAttributes> VertexBuffers[] : register(t0, space1);
Buffer<uint> IndexBuffers[] : register(t0, space2);
Texture2D<float4> Textures[] : register(t0, space3);
Texture2D<float4> GBufferTextures[] : register(t0, space4);
TextureCube Skybox : register(t0, space5);

RWTexture2D<float4> Output : register(u0, space0);
RWTexture2D<float4> Accumulation : register(u1, space0);
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
    uint Camera_AccumulationFrameIndex;
    uint Camera_Padding;
    PathTracingLightData Camera_Lights[MaxPathTracingLights];
};

#endif
