#include <ShaderLibrary/Common/RootSignature.hlsli>

TextureCube SkyboxTexture : register(t0);

struct PixelShaderInput
{
    float3 CubemapUv : CUBEMAP_UV;
};

float4 main(PixelShaderInput IN) : SV_TARGET
{
    return SkyboxTexture.Sample(g_Common_LinearClampSampler, IN.CubemapUv);
}
