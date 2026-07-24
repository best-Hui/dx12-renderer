#include "ComputeShader.h"
#include <DX12Library/Helpers.h>
#include <DX12Library/ShaderUtils.h>
#include <DX12Library/Application.h>
#include <Framework/RayTracingAccelerationStructure.h>

#include <algorithm>

//Modify Begin:2026-07-23 by BestHui
namespace
{
    UINT GetDescriptorCountOverride(const ComputePipelineDesc& desc, const std::string& name, UINT reflectedCount)
    {
        for (const ComputePipelineDesc::BindingOverride& bindingOverride : desc.BindingOverrides)
        {
            if (bindingOverride.Name == name)
            {
                return bindingOverride.DescriptorCount;
            }
        }

        return DescriptorLayout::NormalizeDescriptorCount(reflectedCount, desc.MaxDescriptorCount);
    }
}
//Modify End

//Modify Begin:2026-07-23 by BestHui
ComputePipelineDescBuilder::ComputePipelineDescBuilder(ComputePipelineDesc desc)
    : m_Desc(std::move(desc))
{
}

ComputePipelineDescBuilder ComputePipelineDescBuilder::ReflectedDefault(const ShaderBlob&)
{
    return ComputePipelineDescBuilder(ComputePipelineDesc{});
}

ComputePipelineDescBuilder& ComputePipelineDescBuilder::WithDescriptorArrayCount(std::string name, UINT descriptorCount)
{
    const auto existingOverride = std::find_if(
        m_Desc.BindingOverrides.begin(),
        m_Desc.BindingOverrides.end(),
        [&name](const ComputePipelineDesc::BindingOverride& bindingOverride)
        {
            return bindingOverride.Name == name;
        });

    if (existingOverride != m_Desc.BindingOverrides.end())
    {
        existingOverride->DescriptorCount = descriptorCount;
        return *this;
    }

    m_Desc.BindingOverrides.push_back({ std::move(name), descriptorCount });
    return *this;
}

ComputePipelineDescBuilder& ComputePipelineDescBuilder::WithMaxDescriptorCount(UINT maxDescriptorCount)
{
    m_Desc.MaxDescriptorCount = maxDescriptorCount;
    return *this;
}

ComputePipelineDesc ComputePipelineDescBuilder::Build() const
{
    return m_Desc;
}

ComputeShader::ComputeShader(
    const std::shared_ptr<CommonRootSignature>& rootSignature,
    const ShaderBlob& shader,
    bool collectMetadata)
    : m_CommonRootSignature(rootSignature)
    , m_RootSignature(rootSignature)
    , m_PipelineStateBuilder(rootSignature)
    , m_Shader(shader.GetBlob())
{
    m_PipelineStateBuilder.WithShader(m_Shader);
    if (collectMetadata)
    {
        CollectShaderMetadata(m_Shader, &m_ShaderMetadata);
    }
}

ComputeShader::ComputeShader(const ShaderBlob& shader, ComputePipelineDesc desc)
    : m_Shader(shader.GetBlob())
    , m_DescriptorLayout(std::make_unique<DescriptorLayout>())
    , m_UseReflectedRootSignature(true)
{
    CollectShaderMetadata(m_Shader, &m_ShaderMetadata);
    BuildReflectedRootSignature(desc);
    m_PipelineStateBuilder.WithRootSignature(m_RootSignature).WithShader(m_Shader);
}
//Modify End

void ComputeShader::Bind(CommandList& commandList) const
{
    //Modify Begin:2026-07-23 by BestHui
    const auto device = Application::Get().GetDevice();
    const auto pipelineState = GetPipelineState(device);

    commandList.SetComputeRootSignature(*m_RootSignature);
    if (m_UseReflectedRootSignature)
    {
        m_DescriptorLayout->StageDefaultDescriptorTables(commandList);
    }
    commandList.SetPipelineState(pipelineState);
    //Modify End
}

//Modify Begin:2026-07-23 by BestHui
void ComputeShader::Unbind(CommandList& commandList) const
{
    if (m_CommonRootSignature)
    {
        m_CommonRootSignature->UnbindComputeShaderResourceViews(commandList);
    }
}

void ComputeShader::SetMaterialConstantBuffer(CommandList& commandList, size_t size, const void* data) const
{
    if (m_UseReflectedRootSignature)
    {
        SetComputeConstantBuffer(commandList, size, data);
        return;
    }

    m_CommonRootSignature->SetComputeMaterialConstantBuffer(commandList, size, data);
}

void ComputeShader::SetComputeConstantBuffer(CommandList& commandList, size_t size, const void* data) const
{
    if (m_UseReflectedRootSignature)
    {
        const DescriptorBindingInfo& constantBufferBinding = m_DescriptorLayout->GetFirstBinding(DescriptorBindingKind::ConstantBuffer);
        commandList.SetComputeDynamicConstantBuffer(constantBufferBinding.RootParameterIndex, size, data);
        return;
    }

    m_CommonRootSignature->SetComputeConstantBuffer(commandList, size, data);
}

void ComputeShader::SetConstantBuffer(CommandList& commandList, const std::string& variableName, size_t size, const void* data) const
{
    if (m_UseReflectedRootSignature)
    {
        const DescriptorBindingInfo& binding = GetReflectedBinding(variableName, DescriptorBindingKind::ConstantBuffer);
        commandList.SetComputeDynamicConstantBuffer(binding.RootParameterIndex, size, data);
        return;
    }

    const auto findResult = m_ShaderMetadata.m_ConstantBuffersNameCache.find(variableName);
    if (findResult == m_ShaderMetadata.m_ConstantBuffersNameCache.end())
    {
        throw std::exception("Shader variable not found.");
    }

    const ShaderUtils::ConstantBufferMetadata& cbufferMetadata = m_ShaderMetadata.m_ConstantBuffers[findResult->second];
    switch (cbufferMetadata.Space)
    {
    case CommonRootSignature::MATERIAL_REGISTER_SPACE:
        m_CommonRootSignature->SetComputeMaterialConstantBuffer(commandList, size, data);
        break;
    case CommonRootSignature::MODEL_REGISTER_SPACE:
        m_CommonRootSignature->SetComputeModelConstantBuffer(commandList, size, data);
        break;
    case CommonRootSignature::PIPELINE_REGISTER_SPACE:
        m_CommonRootSignature->SetComputePipelineConstantBuffer(commandList, size, data);
        break;
    default:
        throw std::exception("Invalid space index for a constant buffer.");
    }
}

void ComputeShader::SetShaderResourceView(CommandList& commandList, const std::string& variableName, const ShaderResourceView& shaderResourceView) const
{
    SetShaderResourceView(commandList, variableName, 0u, shaderResourceView);
}

void ComputeShader::SetTexture(CommandList& commandList, const std::string& variableName, const ShaderResourceView& shaderResourceView) const
{
    SetShaderResourceView(commandList, variableName, shaderResourceView);
}

void ComputeShader::SetTexture(CommandList& commandList, const std::string& variableName, const std::shared_ptr<Resource>& texture) const
{
    SetTexture(commandList, variableName, ShaderResourceView(texture));
}

void ComputeShader::SetShaderResourceView(CommandList& commandList, const std::string& variableName, UINT arrayIndex, const ShaderResourceView& shaderResourceView) const
{
    if (m_UseReflectedRootSignature)
    {
        const DescriptorBindingInfo& binding = GetReflectedBinding(variableName, DescriptorBindingKind::ShaderResourceView);
        if (!DescriptorLayout::IsArrayIndexInBounds(binding.DescriptorCount, arrayIndex))
        {
            throw std::exception("SRV array index is out of bounds.");
        }

        if (shaderResourceView.m_Resource->AreAutoBarriersEnabled())
        {
            commandList.SetShaderResourceView(
                binding.RootParameterIndex,
                arrayIndex,
                *shaderResourceView.m_Resource,
                D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE,
                shaderResourceView.m_FirstSubresource,
                shaderResourceView.m_NumSubresources,
                shaderResourceView.GetDescOrNullptr());
        }
        else
        {
            commandList.SetShaderResourceView(
                binding.RootParameterIndex,
                arrayIndex,
                *shaderResourceView.m_Resource,
                shaderResourceView.m_FirstSubresource,
                shaderResourceView.m_NumSubresources,
                shaderResourceView.GetDescOrNullptr());
        }
        return;
    }

    const auto findResult = m_ShaderMetadata.m_ShaderResourceViewsNameCache.find(variableName);
    if (findResult == m_ShaderMetadata.m_ShaderResourceViewsNameCache.end())
    {
        throw std::exception("Shader variable not found.");
    }

    const auto index = findResult->second;
    const auto& srvMetadata = m_ShaderMetadata.m_ShaderResourceViews[index];
    if (!DescriptorLayout::IsArrayIndexInBounds(srvMetadata.BindCount, arrayIndex))
    {
        throw std::exception("SRV array index is out of bounds.");
    }

    const UINT registerIndex = srvMetadata.RegisterIndex + arrayIndex;
    switch (srvMetadata.Space)
    {
    case CommonRootSignature::MATERIAL_REGISTER_SPACE:
        m_CommonRootSignature->SetComputeShaderResourceView(commandList, registerIndex, shaderResourceView);
        break;
    case CommonRootSignature::PIPELINE_REGISTER_SPACE:
        m_CommonRootSignature->SetPipelineShaderResourceView(commandList, registerIndex, shaderResourceView);
        break;
    default:
        throw std::exception("Invalid space index for an SRV.");
    }
}

void ComputeShader::SetShaderResourceView(CommandList& commandList, const std::string& variableName, UINT arrayIndex, const Resource& resource, D3D12_RESOURCE_STATES stateAfter) const
{
    if (m_UseReflectedRootSignature)
    {
        const DescriptorBindingInfo& binding = GetReflectedBinding(variableName, DescriptorBindingKind::ShaderResourceView);
        if (!DescriptorLayout::IsArrayIndexInBounds(binding.DescriptorCount, arrayIndex))
        {
            throw std::exception("SRV array index is out of bounds.");
        }

        commandList.SetShaderResourceView(binding.RootParameterIndex, arrayIndex, resource, stateAfter);
        return;
    }

    const auto findResult = m_ShaderMetadata.m_ShaderResourceViewsNameCache.find(variableName);
    if (findResult == m_ShaderMetadata.m_ShaderResourceViewsNameCache.end())
    {
        throw std::exception("Shader variable not found.");
    }

    const auto index = findResult->second;
    const auto& srvMetadata = m_ShaderMetadata.m_ShaderResourceViews[index];
    if (!DescriptorLayout::IsArrayIndexInBounds(srvMetadata.BindCount, arrayIndex))
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
    Assert(m_CommonRootSignature != nullptr, "Pipeline SRV index binding requires the common root signature.");
    m_CommonRootSignature->SetPipelineShaderResourceView(commandList, index, shaderResourceView);
}

void ComputeShader::SetPipelineShaderResourceView(CommandList& commandList, UINT index, const Resource& resource, D3D12_RESOURCE_STATES stateAfter) const
{
    Assert(m_CommonRootSignature != nullptr, "Pipeline SRV index binding requires the common root signature.");
    commandList.SetShaderResourceView(CommonRootSignature::RootParameters::PipelineSRVs, index, resource, stateAfter);
}

void ComputeShader::SetComputeShaderResourceView(CommandList& commandList, UINT index, const ShaderResourceView& shaderResourceView) const
{
    Assert(m_CommonRootSignature != nullptr, "Compute SRV index binding requires the common root signature.");
    m_CommonRootSignature->SetComputeShaderResourceView(commandList, index, shaderResourceView);
}

void ComputeShader::SetUnorderedAccessView(CommandList& commandList, UINT index, const UnorderedAccessView& unorderedAccessView) const
{
    Assert(m_CommonRootSignature != nullptr, "UAV index binding requires the common root signature.");
    m_CommonRootSignature->SetUnorderedAccessView(commandList, index, unorderedAccessView);
}

void ComputeShader::SetUnorderedAccessView(CommandList& commandList, const std::string& variableName, const UnorderedAccessView& unorderedAccessView) const
{
    if (m_UseReflectedRootSignature)
    {
        const DescriptorBindingInfo& binding = GetReflectedBinding(variableName, DescriptorBindingKind::UnorderedAccessView);
        Assert(binding.DescriptorCount == 1u, "Named compute UAV binding currently expects a single descriptor.");

        if (unorderedAccessView.m_Resource->AreAutoBarriersEnabled())
        {
            commandList.SetUnorderedAccessView(
                binding.RootParameterIndex,
                0u,
                *unorderedAccessView.m_Resource,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                unorderedAccessView.m_FirstSubresource,
                unorderedAccessView.m_NumSubresources,
                unorderedAccessView.GetDescOrNullptr());
        }
        else
        {
            commandList.SetUnorderedAccessView(
                binding.RootParameterIndex,
                0u,
                *unorderedAccessView.m_Resource,
                unorderedAccessView.m_FirstSubresource,
                unorderedAccessView.m_NumSubresources,
                unorderedAccessView.GetDescOrNullptr());
        }
        return;
    }

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

    m_CommonRootSignature->SetUnorderedAccessView(commandList, uavMetadata.RegisterIndex, unorderedAccessView);
}

void ComputeShader::SetAccelerationStructure(CommandList& commandList, const RayTracingAccelerationStructure& accelerationStructure) const
{
    if (m_UseReflectedRootSignature)
    {
        const DescriptorBindingInfo& accelerationStructureBinding = m_DescriptorLayout->GetFirstBinding(DescriptorBindingKind::AccelerationStructure);
        Assert(accelerationStructure.IsBuilt(), "Ray tracing acceleration structure is not built.");
        commandList.SetComputeRootShaderResourceView(
            accelerationStructureBinding.RootParameterIndex,
            accelerationStructure.GetGpuVirtualAddress());
        return;
    }

    m_CommonRootSignature->SetComputeAccelerationStructure(commandList, accelerationStructure);
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
    *outMetadata = ShaderReflection::CollectShader(shader);
}

void ComputeShader::BuildReflectedRootSignature(const ComputePipelineDesc& desc)
{
    using RootParameter = CD3DX12_ROOT_PARAMETER1;
    using DescriptorRange = CD3DX12_DESCRIPTOR_RANGE1;
    using StaticSampler = CD3DX12_STATIC_SAMPLER_DESC;

    std::vector<DescriptorRange> descriptorRanges;
    descriptorRanges.reserve(m_ShaderMetadata.m_ShaderResourceViews.size() + m_ShaderMetadata.m_UnorderedAccessViews.size());

    std::vector<RootParameter> rootParameters;
    rootParameters.reserve(
        m_ShaderMetadata.m_ConstantBuffers.size() +
        m_ShaderMetadata.m_ShaderResourceViews.size() +
        m_ShaderMetadata.m_UnorderedAccessViews.size());

    const auto addBindingInfo = [this](const std::string& name, DescriptorBindingInfo binding)
    {
        m_DescriptorLayout->AddBinding(name, binding);
    };

    for (const auto& cbuffer : m_ShaderMetadata.m_ConstantBuffers)
    {
        RootParameter rootParameter;
        rootParameter.InitAsConstantBufferView(cbuffer.RegisterIndex, cbuffer.Space);
        const UINT rootParameterIndex = static_cast<UINT>(rootParameters.size());
        rootParameters.push_back(rootParameter);
        addBindingInfo(cbuffer.Name, { DescriptorBindingKind::ConstantBuffer, rootParameterIndex, 1u });
    }

    for (const auto& srv : m_ShaderMetadata.m_ShaderResourceViews)
    {
        RootParameter rootParameter;
        DescriptorBindingKind bindingKind = DescriptorBindingKind::ShaderResourceView;
        UINT descriptorCount = 1u;

        if (DescriptorLayout::IsRayTracingAccelerationStructureSrv(srv, "g_InlineRayTracingScene"))
        {
            rootParameter.InitAsShaderResourceView(srv.RegisterIndex, srv.Space);
            bindingKind = DescriptorBindingKind::AccelerationStructure;
        }
        else
        {
            descriptorCount = GetDescriptorCountOverride(desc, srv.Name, srv.BindCount);
            descriptorRanges.emplace_back(
                D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
                descriptorCount,
                srv.RegisterIndex,
                srv.Space,
                D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);
            rootParameter.InitAsDescriptorTable(1, &descriptorRanges.back(), D3D12_SHADER_VISIBILITY_ALL);
        }

        const UINT rootParameterIndex = static_cast<UINT>(rootParameters.size());
        rootParameters.push_back(rootParameter);
        addBindingInfo(srv.Name, { bindingKind, rootParameterIndex, descriptorCount });
        if (bindingKind == DescriptorBindingKind::ShaderResourceView)
        {
            m_DescriptorLayout->AddDefaultShaderResourceViewTable(rootParameterIndex, descriptorCount, srv);
        }
    }

    for (const auto& uav : m_ShaderMetadata.m_UnorderedAccessViews)
    {
        const UINT descriptorCount = GetDescriptorCountOverride(desc, uav.Name, uav.BindCount);
        descriptorRanges.emplace_back(
            D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
            descriptorCount,
            uav.RegisterIndex,
            uav.Space,
            D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);

        RootParameter rootParameter;
        rootParameter.InitAsDescriptorTable(1, &descriptorRanges.back(), D3D12_SHADER_VISIBILITY_ALL);
        const UINT rootParameterIndex = static_cast<UINT>(rootParameters.size());
        rootParameters.push_back(rootParameter);
        addBindingInfo(uav.Name, { DescriptorBindingKind::UnorderedAccessView, rootParameterIndex, descriptorCount });
        m_DescriptorLayout->AddDefaultUnorderedAccessViewTable(rootParameterIndex, descriptorCount, uav);
    }

    const StaticSampler staticSamplers[] =
    {
        StaticSampler(0, D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP),
        StaticSampler(1, D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP),
        StaticSampler(2, D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP),
        StaticSampler(3, D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP),
        StaticSampler(4, D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 0, 16,
            D3D12_COMPARISON_FUNC_LESS_EQUAL)
    };

    D3D12_ROOT_SIGNATURE_DESC1 rootSignatureDesc{};
    rootSignatureDesc.NumParameters = static_cast<UINT>(rootParameters.size());
    rootSignatureDesc.pParameters = rootParameters.data();
    rootSignatureDesc.NumStaticSamplers = _countof(staticSamplers);
    rootSignatureDesc.pStaticSamplers = staticSamplers;
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    m_RootSignature = std::make_shared<RootSignature>(rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1_1);
}

const DescriptorBindingInfo& ComputeShader::GetReflectedBinding(const std::string& variableName, DescriptorBindingKind expectedKind) const
{
    return m_DescriptorLayout->GetBinding(variableName, expectedKind);
}
//Modify End
