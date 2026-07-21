#include <ShaderLibrary/Common/RootSignature.hlsli>

cbuffer ModelCBuffer : register(b0, COMMON_ROOT_SIGNATURE_MODEL_SPACE)
{
    matrix g_Model_Model;
    matrix g_Model_ModelViewProjection;
    matrix g_Model_InverseTransposeModel;
    matrix g_Model_Padding;
};

struct VertexShaderOutput
{
    float3 CubemapUv : CUBEMAP_UV;
    float4 PositionCs : SV_POSITION;
};

VertexShaderOutput main(float3 positionOs : POSITION)
{
    VertexShaderOutput OUT;
    OUT.CubemapUv = positionOs;

    float4 positionCs = mul(g_Model_ModelViewProjection, float4(positionOs, 1.0f));
    positionCs.z = 0.9999f * positionCs.w;
    OUT.PositionCs = positionCs;
    return OUT;
}
