#pragma once

#include <memory.h>

#include <d3d12.h>
#include <d3dx12.h>
#include <wrl.h>

#include <DX12Library/CommandList.h>
//Modify Begin:2026-07-23 by BestHui
#include <DX12Library/ShaderUtils.h>
//Modify End

#include "CommonRootSignature.h"
#include "ComputePipelineStateBuilder.h"
#include "DescriptorLayout.h"
#include "ShaderBlob.h"
#include "ShaderReflection.h"
//Modify Begin:2026-07-23 by BestHui
#include "ShaderResourceView.h"
#include "UnorderedAccessView.h"

#include <map>
#include <string>
#include <vector>
//Modify End

//Modify Begin:2026-07-23 by BestHui
struct ComputePipelineDesc
{
    struct BindingOverride
    {
        std::string Name;
        UINT DescriptorCount = 1;
    };

    std::vector<BindingOverride> BindingOverrides;
    UINT MaxDescriptorCount = 1024;
};

class ComputePipelineDescBuilder
{
public:
    static ComputePipelineDescBuilder ReflectedDefault(const ShaderBlob& shader);

    ComputePipelineDescBuilder& WithDescriptorArrayCount(std::string name, UINT descriptorCount);
    ComputePipelineDescBuilder& WithMaxDescriptorCount(UINT maxDescriptorCount);

    ComputePipelineDesc Build() const;

private:
    explicit ComputePipelineDescBuilder(ComputePipelineDesc desc);

    ComputePipelineDesc m_Desc;
};
//Modify End

class ComputeShader
{
public:
    //Modify Begin:2026-07-23 by BestHui
    explicit ComputeShader(
        const std::shared_ptr<CommonRootSignature>& rootSignature,
        const ShaderBlob& shader,
        bool collectMetadata = true);
    ComputeShader(const ShaderBlob& shader, ComputePipelineDesc desc);
    //Modify End

    void Bind(CommandList& commandList) const;
    //Modify Begin:2026-07-23 by BestHui
    void Unbind(CommandList& commandList) const;

    template<typename T>
    void SetPipelineConstantBuffer(CommandList& commandList, const T& data) const
    {
        m_CommonRootSignature->SetComputePipelineConstantBuffer(commandList, data);
    }

    template<typename T>
    void SetModelConstantBuffer(CommandList& commandList, const T& data) const
    {
        m_CommonRootSignature->SetComputeModelConstantBuffer(commandList, data);
    }

    void SetMaterialConstantBuffer(CommandList& commandList, size_t size, const void* data) const;

    template<typename T>
    void SetComputeConstantBuffer(CommandList& commandList, const T& data) const
    {
        SetComputeConstantBuffer(commandList, sizeof(T), &data);
    }

    void SetComputeConstantBuffer(CommandList& commandList, size_t size, const void* data) const;
    void SetConstantBuffer(CommandList& commandList, const std::string& variableName, size_t size, const void* data) const;

    template<typename T>
    void SetConstantBuffer(CommandList& commandList, const std::string& variableName, const T& data) const
    {
        SetConstantBuffer(commandList, variableName, sizeof(T), &data);
    }

    void SetShaderResourceView(CommandList& commandList, const std::string& variableName, const ShaderResourceView& shaderResourceView) const;
    void SetShaderResourceView(CommandList& commandList, const std::string& variableName, UINT arrayIndex, const ShaderResourceView& shaderResourceView) const;
    void SetShaderResourceView(CommandList& commandList, const std::string& variableName, UINT arrayIndex, const Resource& resource, D3D12_RESOURCE_STATES stateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) const;
    void SetTexture(CommandList& commandList, const std::string& variableName, const ShaderResourceView& shaderResourceView) const;
    void SetTexture(CommandList& commandList, const std::string& variableName, const std::shared_ptr<Resource>& texture) const;

    void SetPipelineShaderResourceView(CommandList& commandList, UINT index, const ShaderResourceView& shaderResourceView) const;
    void SetPipelineShaderResourceView(CommandList& commandList, UINT index, const Resource& resource, D3D12_RESOURCE_STATES stateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) const;
    void SetComputeShaderResourceView(CommandList& commandList, UINT index, const ShaderResourceView& shaderResourceView) const;
    void SetUnorderedAccessView(CommandList& commandList, UINT index, const UnorderedAccessView& unorderedAccessView) const;
    void SetUnorderedAccessView(CommandList& commandList, const std::string& variableName, const UnorderedAccessView& unorderedAccessView) const;
    void SetAccelerationStructure(CommandList& commandList, const RayTracingAccelerationStructure& accelerationStructure) const;

    using ShaderMetadata = ShaderReflectionMetadata;

    const ShaderMetadata& GetShaderMetadata() const { return m_ShaderMetadata; }
    //Modify End


private:
    //Modify Begin:2026-07-23 by BestHui
    Microsoft::WRL::ComPtr<ID3D12PipelineState> GetPipelineState(const Microsoft::WRL::ComPtr<ID3D12Device2>& device) const;

    void CollectShaderMetadata(const Microsoft::WRL::ComPtr<ID3DBlob>& shader, ShaderMetadata* outMetadata);
    void BuildReflectedRootSignature(const ComputePipelineDesc& desc);
    const DescriptorBindingInfo& GetReflectedBinding(const std::string& variableName, DescriptorBindingKind expectedKind) const;

    std::shared_ptr<CommonRootSignature> m_CommonRootSignature;
    std::shared_ptr<RootSignature> m_RootSignature;
    ComputePipelineStateBuilder m_PipelineStateBuilder;
    Microsoft::WRL::ComPtr<ID3DBlob> m_Shader;
    ShaderMetadata m_ShaderMetadata;
    std::unique_ptr<DescriptorLayout> m_DescriptorLayout;
    bool m_UseReflectedRootSignature = false;
    mutable Microsoft::WRL::ComPtr<ID3D12PipelineState> m_PipelineState;
    //Modify End
};
