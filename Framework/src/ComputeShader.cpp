#include "ComputeShader.h"
#include <DX12Library/Helpers.h>
#include <DX12Library/ShaderUtils.h>
#include <DX12Library/Application.h>

//Modify Begin:2026-07-23 by BestHui
ComputeShader::ComputeShader(
    const std::shared_ptr<CommonRootSignature>& rootSignature,
    const ShaderBlob& shader,
    bool collectMetadata)
    : m_RootSignature(rootSignature)
    , m_Shader(shader.GetBlob())
{
    if (collectMetadata)
    {
        CollectShaderMetadata(m_Shader, &m_ShaderMetadata);
    }
}
//Modify End

void ComputeShader::Bind(CommandList& commandList) const
{
    //Modify Begin:2026-07-23 by BestHui
    const auto device = Application::Get().GetDevice();
    const auto pipelineState = GetPipelineState(device);

    commandList.SetPipelineState(pipelineState);
    //Modify End
}

//Modify Begin:2026-07-23 by BestHui
void ComputeShader::Unbind(CommandList& commandList) const
{
    m_RootSignature->UnbindMaterialShaderResourceViews(commandList);
}

void ComputeShader::SetMaterialConstantBuffer(CommandList& commandList, size_t size, const void* data) const
{
    m_RootSignature->SetComputeMaterialConstantBuffer(commandList, size, data);
}

void ComputeShader::SetComputeConstantBuffer(CommandList& commandList, size_t size, const void* data) const
{
    m_RootSignature->SetComputeConstantBuffer(commandList, size, data);
}

void ComputeShader::SetShaderResourceView(CommandList& commandList, const std::string& variableName, const ShaderResourceView& shaderResourceView) const
{
    const auto findResult = m_ShaderMetadata.m_ShaderResourceViewsNameCache.find(variableName);
    if (findResult == m_ShaderMetadata.m_ShaderResourceViewsNameCache.end())
    {
        throw std::exception("Shader variable not found.");
    }

    const auto index = findResult->second;
    const auto& srvMetadata = m_ShaderMetadata.m_ShaderResourceViews[index];
    switch (srvMetadata.Space)
    {
    case CommonRootSignature::MATERIAL_REGISTER_SPACE:
        m_RootSignature->SetComputeShaderResourceView(commandList, srvMetadata.RegisterIndex, shaderResourceView);
        break;
    case CommonRootSignature::PIPELINE_REGISTER_SPACE:
        m_RootSignature->SetPipelineShaderResourceView(commandList, srvMetadata.RegisterIndex, shaderResourceView);
        break;
    default:
        throw std::exception("Invalid space index for an SRV.");
    }
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> ComputeShader::GetPipelineState(const Microsoft::WRL::ComPtr<ID3D12Device2>& device) const
{
    if (m_PipelineState)
    {
        return m_PipelineState;
    }

    {
        struct PipelineStateStream
        {
            CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE RootSignature;
            CD3DX12_PIPELINE_STATE_STREAM_CS CS;
        } pipelineStateStream;

        pipelineStateStream.RootSignature = m_RootSignature->GetRootSignature().Get();

        pipelineStateStream.CS = CD3DX12_SHADER_BYTECODE(m_Shader->GetBufferPointer(), m_Shader->GetBufferSize());

        const D3D12_PIPELINE_STATE_STREAM_DESC pipelineStateStreamDesc{ sizeof(PipelineStateStream), &pipelineStateStream };
        ThrowIfFailed(device->CreatePipelineState(&pipelineStateStreamDesc, IID_PPV_ARGS(&m_PipelineState)));
    }

    return m_PipelineState;
}

void ComputeShader::CollectShaderMetadata(const Microsoft::WRL::ComPtr<ID3DBlob>& shader, ShaderMetadata* outMetadata)
{
    const auto reflection = ShaderUtils::Reflect(shader);

    {
        outMetadata->m_ConstantBuffers = std::move(ShaderUtils::GetConstantBuffers(reflection));

        for (size_t i = 0; i < outMetadata->m_ConstantBuffers.size(); ++i)
        {
            const auto& cbufferMetadata = outMetadata->m_ConstantBuffers[i];
            outMetadata->m_ConstantBuffersNameCache.emplace(cbufferMetadata.Name, i);
        }
    }

    {
        outMetadata->m_ShaderResourceViews = std::move(ShaderUtils::GetShaderResourceViews(reflection));

        for (size_t i = 0; i < outMetadata->m_ShaderResourceViews.size(); ++i)
        {
            const auto& srvMetadata = outMetadata->m_ShaderResourceViews[i];
            outMetadata->m_ShaderResourceViewsNameCache.emplace(srvMetadata.Name, i);
        }
    }
}
//Modify End
