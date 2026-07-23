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
#include "ShaderBlob.h"
//Modify Begin:2026-07-23 by BestHui
#include "ShaderResourceView.h"
#include "UnorderedAccessView.h"

#include <map>
#include <string>
#include <vector>
//Modify End

class ComputeShader
{
public:
    //Modify Begin:2026-07-23 by BestHui
    explicit ComputeShader(
        const std::shared_ptr<CommonRootSignature>& rootSignature,
        const ShaderBlob& shader,
        bool collectMetadata = true);
    //Modify End

    void Bind(CommandList& commandList) const;
    //Modify Begin:2026-07-23 by BestHui
    void Unbind(CommandList& commandList) const;

    template<typename T>
    void SetPipelineConstantBuffer(CommandList& commandList, const T& data) const
    {
        m_RootSignature->SetComputePipelineConstantBuffer(commandList, data);
    }

    template<typename T>
    void SetModelConstantBuffer(CommandList& commandList, const T& data) const
    {
        m_RootSignature->SetComputeModelConstantBuffer(commandList, data);
    }

    void SetMaterialConstantBuffer(CommandList& commandList, size_t size, const void* data) const;

    template<typename T>
    void SetComputeConstantBuffer(CommandList& commandList, const T& data) const
    {
        m_RootSignature->SetComputeConstantBuffer(commandList, data);
    }

    void SetComputeConstantBuffer(CommandList& commandList, size_t size, const void* data) const;

    void SetShaderResourceView(CommandList& commandList, const std::string& variableName, const ShaderResourceView& shaderResourceView) const;
    void SetShaderResourceView(CommandList& commandList, const std::string& variableName, UINT arrayIndex, const ShaderResourceView& shaderResourceView) const;
    void SetShaderResourceView(CommandList& commandList, const std::string& variableName, UINT arrayIndex, const Resource& resource, D3D12_RESOURCE_STATES stateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) const;

    void SetPipelineShaderResourceView(CommandList& commandList, UINT index, const ShaderResourceView& shaderResourceView) const;
    void SetPipelineShaderResourceView(CommandList& commandList, UINT index, const Resource& resource, D3D12_RESOURCE_STATES stateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) const;
    void SetComputeShaderResourceView(CommandList& commandList, UINT index, const ShaderResourceView& shaderResourceView) const;
    void SetUnorderedAccessView(CommandList& commandList, UINT index, const UnorderedAccessView& unorderedAccessView) const;
    void SetUnorderedAccessView(CommandList& commandList, const std::string& variableName, const UnorderedAccessView& unorderedAccessView) const;
    void SetAccelerationStructure(CommandList& commandList, const RayTracingAccelerationStructure& accelerationStructure) const;

    struct ShaderMetadata
    {
        using NameCacheMap = std::map<std::string, size_t>;

        std::vector<ShaderUtils::ConstantBufferMetadata> m_ConstantBuffers{};
        NameCacheMap m_ConstantBuffersNameCache{};

        std::vector<ShaderUtils::ShaderResourceViewMetadata> m_ShaderResourceViews{};
        NameCacheMap m_ShaderResourceViewsNameCache{};

        std::vector<ShaderUtils::UnorderedAccessViewMetadata> m_UnorderedAccessViews{};
        NameCacheMap m_UnorderedAccessViewsNameCache{};
    };

    const ShaderMetadata& GetShaderMetadata() const { return m_ShaderMetadata; }
    //Modify End

private:
    //Modify Begin:2026-07-23 by BestHui
    Microsoft::WRL::ComPtr<ID3D12PipelineState> GetPipelineState(const Microsoft::WRL::ComPtr<ID3D12Device2>& device) const;

    void CollectShaderMetadata(const Microsoft::WRL::ComPtr<ID3DBlob>& shader, ShaderMetadata* outMetadata);

    std::shared_ptr<CommonRootSignature> m_RootSignature;
    ComputePipelineStateBuilder m_PipelineStateBuilder;
    Microsoft::WRL::ComPtr<ID3DBlob> m_Shader;
    ShaderMetadata m_ShaderMetadata;
    mutable Microsoft::WRL::ComPtr<ID3D12PipelineState> m_PipelineState;
    //Modify End
};
