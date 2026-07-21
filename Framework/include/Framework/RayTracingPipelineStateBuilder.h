#pragma once
//Modify Begin:2026-07-21 by BestHui

#include <Framework/RayTracingShader.h>

#include <DX12Library/RootSignature.h>

#include <d3d12.h>
#include <wrl.h>

#include <memory>
#include <string>

class ShaderBlob;

class RayTracingPipelineState
{
public:
    RootSignature& GetGlobalRootSignature() const;
    const Microsoft::WRL::ComPtr<ID3D12StateObject>& GetStateObject() const;
    const void* GetShaderIdentifier(const std::wstring& shaderName) const;

private:
    friend class RayTracingPipelineStateBuilder;

    std::shared_ptr<RootSignature> m_GlobalRootSignature;
    Microsoft::WRL::ComPtr<ID3D12StateObject> m_StateObject;
    Microsoft::WRL::ComPtr<ID3D12StateObjectProperties> m_StateObjectProperties;
};

class RayTracingPipelineStateBuilder
{
public:
    RayTracingPipelineStateBuilder(const ShaderBlob& shaderLibrary, RayTracingPipelineDesc desc);

    std::shared_ptr<RayTracingPipelineState> Build() const;

private:
    std::shared_ptr<RootSignature> BuildGlobalRootSignature() const;
    Microsoft::WRL::ComPtr<ID3D12StateObject> BuildStateObject(const std::shared_ptr<RootSignature>& globalRootSignature) const;
    size_t BuildCacheKey() const;

    const ShaderBlob& m_ShaderLibrary;
    RayTracingPipelineDesc m_Desc;
};
//Modify End
