#include <ShaderLibrary/Common/RootSignature.hlsli>

struct PixelShaderInput
{
    float3 PositionWs : POSITION_WS;
    float3 NormalWs : NORMAL;
    float2 Uv : TEXCOORD0;
};

struct PixelShaderOutput
{
    float4 Diffuse : SV_TARGET0;
    float4 Specular : SV_TARGET1;
    float4 NormalRoughness : SV_TARGET2;
    float4 PositionMetallic : SV_TARGET3;
};

cbuffer MaterialCBuffer : register(b0)
{
    float4 Diffuse;
    float4 Specular;
    float4 TilingOffset;
    float Metallic;
    float Roughness;
    float2 Padding;
};

Texture2D DiffuseTexture : register(t0);

float3 EncodeNormal(float3 normal)
{
    return normal * 0.5f + 0.5f;
}

PixelShaderOutput main(PixelShaderInput IN)
{
    PixelShaderOutput OUT;

    const float2 uv = IN.Uv * TilingOffset.xy + TilingOffset.zw;
    const float3 baseColor = Diffuse.rgb * DiffuseTexture.Sample(g_Common_LinearWrapSampler, uv).rgb;
    const float3 normalWs = normalize(IN.NormalWs);
    const float3 specularColor = lerp(Specular.rgb, baseColor, saturate(Metallic));

    OUT.Diffuse = float4(baseColor, 1.0f);
    OUT.Specular = float4(specularColor, 1.0f);
    OUT.NormalRoughness = float4(EncodeNormal(normalWs), saturate(Roughness));
    OUT.PositionMetallic = float4(IN.PositionWs, saturate(Metallic));

    return OUT;
}
