#include "Shader.h"
#include <DX12Library/ShaderUtils.h>
#include <DX12Library/Application.h>

Shader::Shader(
    const std::shared_ptr<CommonRootSignature>& rootSignature,
    const ShaderBlob& vertexShader,
    const ShaderBlob& pixelShader,
    const std::function<void(RasterPipelineStateBuilder&)> buildPipelineState,
//Modify Begin:2026-07-21 by BestHui
    bool collectMetadata)
//Modify End
    : m_RootSignature(rootSignature)
    , m_PipelineStateBuilder(rootSignature)
{
    m_PipelineStateBuilder.WithShaders(vertexShader.GetBlob(), pixelShader.GetBlob());
    buildPipelineState(m_PipelineStateBuilder);

//Modify Begin:2026-07-21 by BestHui
    if (collectMetadata)
    {
        CollectShaderMetadata(vertexShader.GetBlob(), &m_VertexShaderMetadata);
        CollectShaderMetadata(pixelShader.GetBlob(), &m_PixelShaderMetadata);
    }
//Modify End
}

void Shader::Bind(CommandList& commandList)
{
    const auto device = Application::Get().GetDevice();
    const auto& renderTargetState = commandList.GetLastRenderTargetState();
    const auto pipelineState = GetPipelineState(device, renderTargetState);

    commandList.SetPipelineState(pipelineState);
}

void Shader::Unbind(CommandList& commandList)
{
    m_RootSignature->UnbindMaterialShaderResourceViews(commandList);
}

void Shader::SetMaterialConstantBuffer(CommandList& commandList, size_t size, const void* data)
{
    m_RootSignature->SetMaterialConstantBuffer(commandList, size, data);
}

//Modify Begin:2026-07-24 by BestHui
bool Shader::HasConstantBuffer(const std::string& variableName) const
{
    return m_VertexShaderMetadata.m_ConstantBuffersNameCache.find(variableName) != m_VertexShaderMetadata.m_ConstantBuffersNameCache.end() ||
        m_PixelShaderMetadata.m_ConstantBuffersNameCache.find(variableName) != m_PixelShaderMetadata.m_ConstantBuffersNameCache.end();
}

bool Shader::HasShaderResourceView(const std::string& variableName) const
{
    return m_VertexShaderMetadata.m_ShaderResourceViewsNameCache.find(variableName) != m_VertexShaderMetadata.m_ShaderResourceViewsNameCache.end() ||
        m_PixelShaderMetadata.m_ShaderResourceViewsNameCache.find(variableName) != m_PixelShaderMetadata.m_ShaderResourceViewsNameCache.end();
}

bool Shader::HasUnorderedAccessView(const std::string& variableName) const
{
    return m_VertexShaderMetadata.m_UnorderedAccessViewsNameCache.find(variableName) != m_VertexShaderMetadata.m_UnorderedAccessViewsNameCache.end() ||
        m_PixelShaderMetadata.m_UnorderedAccessViewsNameCache.find(variableName) != m_PixelShaderMetadata.m_UnorderedAccessViewsNameCache.end();
}
//Modify End

void Shader::SetConstantBuffer(CommandList& commandList, const std::string& variableName, size_t size, const void* data)
{
    const auto bindConstantBuffer = [this, &commandList, size, data](const ShaderUtils::ConstantBufferMetadata& cbufferMetadata)
    {
        switch (cbufferMetadata.Space)
        {
        case CommonRootSignature::MATERIAL_REGISTER_SPACE:
            m_RootSignature->SetMaterialConstantBuffer(commandList, size, data);
            break;
        case CommonRootSignature::MODEL_REGISTER_SPACE:
            m_RootSignature->SetModelConstantBuffer(commandList, size, data);
            break;
        case CommonRootSignature::PIPELINE_REGISTER_SPACE:
            m_RootSignature->SetPipelineConstantBuffer(commandList, size, data);
            break;
        default:
            throw std::exception("Invalid space index for a constant buffer.");
        }
    };

    bool found = false;
    const auto vsFindResult = m_VertexShaderMetadata.m_ConstantBuffersNameCache.find(variableName);
    if (vsFindResult != m_VertexShaderMetadata.m_ConstantBuffersNameCache.end())
    {
        bindConstantBuffer(m_VertexShaderMetadata.m_ConstantBuffers[vsFindResult->second]);
        found = true;
    }

    const auto psFindResult = m_PixelShaderMetadata.m_ConstantBuffersNameCache.find(variableName);
    if (psFindResult != m_PixelShaderMetadata.m_ConstantBuffersNameCache.end())
    {
        bindConstantBuffer(m_PixelShaderMetadata.m_ConstantBuffers[psFindResult->second]);
        found = true;
    }

    if (!found)
    {
        throw std::exception("Shader variable not found.");
    }
}

void Shader::SetShaderResourceView(CommandList& commandList, const std::string& variableName, const ShaderResourceView& shaderResourceView)
{
    const auto vsFindResult = m_VertexShaderMetadata.m_ShaderResourceViewsNameCache.find(variableName);
    const auto vsFound = vsFindResult != m_VertexShaderMetadata.m_ShaderResourceViewsNameCache.end();
    if (vsFound)
    {
        auto index = vsFindResult->second;
        const auto& srvMetadata = m_VertexShaderMetadata.m_ShaderResourceViews[index];
        switch (srvMetadata.Space)
        {
        case CommonRootSignature::MATERIAL_REGISTER_SPACE:
            m_RootSignature->SetMaterialShaderResourceView(commandList, srvMetadata.RegisterIndex, shaderResourceView);
            break;
        case CommonRootSignature::PIPELINE_REGISTER_SPACE:
            m_RootSignature->SetPipelineShaderResourceView(commandList, srvMetadata.RegisterIndex, shaderResourceView);
            break;
        default:
            throw std::exception("Invalid space index for an SRV.");
        }
    }

    const auto psFindResult = m_PixelShaderMetadata.m_ShaderResourceViewsNameCache.find(variableName);
    const auto psFound = psFindResult != m_PixelShaderMetadata.m_ShaderResourceViewsNameCache.end();
    if (psFound)
    {
        auto index = psFindResult->second;
        const auto& srvMetadata = m_PixelShaderMetadata.m_ShaderResourceViews[index];
        switch (srvMetadata.Space)
        {
        case CommonRootSignature::MATERIAL_REGISTER_SPACE:
            m_RootSignature->SetMaterialShaderResourceView(commandList, srvMetadata.RegisterIndex, shaderResourceView);
            break;
        case CommonRootSignature::PIPELINE_REGISTER_SPACE:
            m_RootSignature->SetPipelineShaderResourceView(commandList, srvMetadata.RegisterIndex, shaderResourceView);
            break;
        default:
            throw std::exception("Invalid space index for an SRV.");
        }
    }

    if (!vsFound && !psFound)
    {
        throw std::exception("Shader variable not found.");
    }
}

void Shader::SetTexture(CommandList& commandList, const std::string& variableName, const ShaderResourceView& shaderResourceView)
{
    SetShaderResourceView(commandList, variableName, shaderResourceView);
}

void Shader::SetTexture(CommandList& commandList, const std::string& variableName, const std::shared_ptr<Resource>& texture)
{
    SetTexture(commandList, variableName, ShaderResourceView(texture));
}

//Modify Begin:2026-07-23 by BestHui
void Shader::SetUnorderedAccessView(CommandList& commandList, const std::string& variableName, const UnorderedAccessView& unorderedAccessView)
{
    const auto vsFindResult = m_VertexShaderMetadata.m_UnorderedAccessViewsNameCache.find(variableName);
    const auto vsFound = vsFindResult != m_VertexShaderMetadata.m_UnorderedAccessViewsNameCache.end();
    if (vsFound)
    {
        const auto index = vsFindResult->second;
        const auto& uavMetadata = m_VertexShaderMetadata.m_UnorderedAccessViews[index];
        if (uavMetadata.Space != CommonRootSignature::MATERIAL_REGISTER_SPACE)
        {
            throw std::exception("Invalid space index for a UAV.");
        }

        m_RootSignature->SetUnorderedAccessView(commandList, uavMetadata.RegisterIndex, unorderedAccessView);
    }

    const auto psFindResult = m_PixelShaderMetadata.m_UnorderedAccessViewsNameCache.find(variableName);
    const auto psFound = psFindResult != m_PixelShaderMetadata.m_UnorderedAccessViewsNameCache.end();
    if (psFound)
    {
        const auto index = psFindResult->second;
        const auto& uavMetadata = m_PixelShaderMetadata.m_UnorderedAccessViews[index];
        if (uavMetadata.Space != CommonRootSignature::MATERIAL_REGISTER_SPACE)
        {
            throw std::exception("Invalid space index for a UAV.");
        }

        m_RootSignature->SetUnorderedAccessView(commandList, uavMetadata.RegisterIndex, unorderedAccessView);
    }

    if (!vsFound && !psFound)
    {
        throw std::exception("Shader variable not found.");
    }
}
//Modify End

Microsoft::WRL::ComPtr<ID3D12PipelineState> Shader::GetPipelineState(const Microsoft::WRL::ComPtr<ID3D12Device2>& device, const RenderTargetState& renderTargetState)
{
    auto findResult = m_PipelineStateObjects.find(renderTargetState);
    if (findResult == m_PipelineStateObjects.end())
    {
        const auto& formats = renderTargetState.GetFormats();
        std::vector<DXGI_FORMAT> renderTargetFormats(formats.GetCount());
        memcpy(renderTargetFormats.data(), formats.GetFormats(), sizeof(DXGI_FORMAT) * renderTargetFormats.size());
        m_PipelineStateBuilder.WithRenderTargetFormats(renderTargetFormats, formats.GetDepthStencilFormat());

        m_PipelineStateBuilder.WithSampleDesc(renderTargetState.GetSampleDesc());

        const auto pipelineStateObject = m_PipelineStateBuilder.Build(device);

        m_PipelineStateObjects.insert(std::make_pair(renderTargetState, pipelineStateObject));

        return pipelineStateObject;
    }
    else
    {
        return findResult->second;
    }
}

void Shader::CollectShaderMetadata(const Microsoft::WRL::ComPtr<ID3DBlob>& shader, ShaderMetadata* outMetadata)
{
    *outMetadata = ShaderReflection::CollectShader(shader);
}
