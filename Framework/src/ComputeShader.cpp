#include "ComputeShader.h"
#include <DX12Library/Helpers.h>
#include <DX12Library/ShaderUtils.h>
#include <DX12Library/Application.h>

//Modify Begin:2026-07-23 by BestHui
namespace
{
    constexpr UINT UnboundedBindCount = 0xffffffffu;

    std::string GetBaseResourceName(const std::string& name)
    {
        const size_t arraySuffix = name.find("[0]");
        if (arraySuffix == std::string::npos)
        {
            return name;
        }

        return name.substr(0, arraySuffix);
    }

    template <typename Metadata>
    void CacheResourceName(const Metadata& metadata, size_t index, ComputeShader::ShaderMetadata::NameCacheMap& cache)
    {
        cache.emplace(metadata.Name, index);

        const std::string baseName = GetBaseResourceName(metadata.Name);
        if (baseName != metadata.Name)
        {
            cache.emplace(baseName, index);
        }
    }

    bool IsArrayIndexInBounds(UINT bindCount, UINT arrayIndex)
    {
        return bindCount == 0u || bindCount == UnboundedBindCount || arrayIndex < bindCount;
    }
}
//Modify End

//Modify Begin:2026-07-23 by BestHui
ComputeShader::ComputeShader(
    const std::shared_ptr<CommonRootSignature>& rootSignature,
    const ShaderBlob& shader,
    bool collectMetadata)
    : m_RootSignature(rootSignature)
    , m_PipelineStateBuilder(rootSignature)
    , m_Shader(shader.GetBlob())
{
    m_PipelineStateBuilder.WithShader(m_Shader);
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
    m_RootSignature->UnbindComputeShaderResourceViews(commandList);
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
    SetShaderResourceView(commandList, variableName, 0u, shaderResourceView);
}

void ComputeShader::SetShaderResourceView(CommandList& commandList, const std::string& variableName, UINT arrayIndex, const ShaderResourceView& shaderResourceView) const
{
    const auto findResult = m_ShaderMetadata.m_ShaderResourceViewsNameCache.find(variableName);
    if (findResult == m_ShaderMetadata.m_ShaderResourceViewsNameCache.end())
    {
        throw std::exception("Shader variable not found.");
    }

    const auto index = findResult->second;
    const auto& srvMetadata = m_ShaderMetadata.m_ShaderResourceViews[index];
    if (!IsArrayIndexInBounds(srvMetadata.BindCount, arrayIndex))
    {
        throw std::exception("SRV array index is out of bounds.");
    }

    const UINT registerIndex = srvMetadata.RegisterIndex + arrayIndex;
    switch (srvMetadata.Space)
    {
    case CommonRootSignature::MATERIAL_REGISTER_SPACE:
        m_RootSignature->SetComputeShaderResourceView(commandList, registerIndex, shaderResourceView);
        break;
    case CommonRootSignature::PIPELINE_REGISTER_SPACE:
        m_RootSignature->SetPipelineShaderResourceView(commandList, registerIndex, shaderResourceView);
        break;
    default:
        throw std::exception("Invalid space index for an SRV.");
    }
}

void ComputeShader::SetShaderResourceView(CommandList& commandList, const std::string& variableName, UINT arrayIndex, const Resource& resource, D3D12_RESOURCE_STATES stateAfter) const
{
    const auto findResult = m_ShaderMetadata.m_ShaderResourceViewsNameCache.find(variableName);
    if (findResult == m_ShaderMetadata.m_ShaderResourceViewsNameCache.end())
    {
        throw std::exception("Shader variable not found.");
    }

    const auto index = findResult->second;
    const auto& srvMetadata = m_ShaderMetadata.m_ShaderResourceViews[index];
    if (!IsArrayIndexInBounds(srvMetadata.BindCount, arrayIndex))
    {
        throw std::exception("SRV array index is out of bounds.");
    }

    const UINT registerIndex = srvMetadata.RegisterIndex + arrayIndex;
    switch (srvMetadata.Space)
    {
    case CommonRootSignature::MATERIAL_REGISTER_SPACE:
        commandList.SetShaderResourceView(CommonRootSignature::RootParameters::MaterialSRVs, registerIndex, resource, stateAfter);
        break;
    case CommonRootSignature::PIPELINE_REGISTER_SPACE:
        commandList.SetShaderResourceView(CommonRootSignature::RootParameters::PipelineSRVs, registerIndex, resource, stateAfter);
        break;
    default:
        throw std::exception("Invalid space index for an SRV.");
    }
}

void ComputeShader::SetPipelineShaderResourceView(CommandList& commandList, UINT index, const ShaderResourceView& shaderResourceView) const
{
    m_RootSignature->SetPipelineShaderResourceView(commandList, index, shaderResourceView);
}

void ComputeShader::SetPipelineShaderResourceView(CommandList& commandList, UINT index, const Resource& resource, D3D12_RESOURCE_STATES stateAfter) const
{
    commandList.SetShaderResourceView(CommonRootSignature::RootParameters::PipelineSRVs, index, resource, stateAfter);
}

void ComputeShader::SetComputeShaderResourceView(CommandList& commandList, UINT index, const ShaderResourceView& shaderResourceView) const
{
    m_RootSignature->SetComputeShaderResourceView(commandList, index, shaderResourceView);
}

void ComputeShader::SetUnorderedAccessView(CommandList& commandList, UINT index, const UnorderedAccessView& unorderedAccessView) const
{
    m_RootSignature->SetUnorderedAccessView(commandList, index, unorderedAccessView);
}

void ComputeShader::SetUnorderedAccessView(CommandList& commandList, const std::string& variableName, const UnorderedAccessView& unorderedAccessView) const
{
    const auto findResult = m_ShaderMetadata.m_UnorderedAccessViewsNameCache.find(variableName);
    if (findResult == m_ShaderMetadata.m_UnorderedAccessViewsNameCache.end())
    {
        throw std::exception("Shader variable not found.");
    }

    const auto index = findResult->second;
    const auto& uavMetadata = m_ShaderMetadata.m_UnorderedAccessViews[index];
    if (uavMetadata.Space != CommonRootSignature::MATERIAL_REGISTER_SPACE)
    {
        throw std::exception("Invalid space index for a UAV.");
    }

    m_RootSignature->SetUnorderedAccessView(commandList, uavMetadata.RegisterIndex, unorderedAccessView);
}

void ComputeShader::SetAccelerationStructure(CommandList& commandList, const RayTracingAccelerationStructure& accelerationStructure) const
{
    m_RootSignature->SetComputeAccelerationStructure(commandList, accelerationStructure);
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> ComputeShader::GetPipelineState(const Microsoft::WRL::ComPtr<ID3D12Device2>& device) const
{
    if (m_PipelineState)
    {
        return m_PipelineState;
    }

    m_PipelineState = m_PipelineStateBuilder.Build(device);

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
            CacheResourceName(cbufferMetadata, i, outMetadata->m_ConstantBuffersNameCache);
        }
    }

    {
        outMetadata->m_ShaderResourceViews = std::move(ShaderUtils::GetShaderResourceViews(reflection));

        for (size_t i = 0; i < outMetadata->m_ShaderResourceViews.size(); ++i)
        {
            const auto& srvMetadata = outMetadata->m_ShaderResourceViews[i];
            CacheResourceName(srvMetadata, i, outMetadata->m_ShaderResourceViewsNameCache);
        }
    }

    {
        outMetadata->m_UnorderedAccessViews = std::move(ShaderUtils::GetUnorderedAccessViews(reflection));

        for (size_t i = 0; i < outMetadata->m_UnorderedAccessViews.size(); ++i)
        {
            const auto& uavMetadata = outMetadata->m_UnorderedAccessViews[i];
            CacheResourceName(uavMetadata, i, outMetadata->m_UnorderedAccessViewsNameCache);
        }
    }
}
//Modify End
