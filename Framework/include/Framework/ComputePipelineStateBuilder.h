#pragma once
//Modify Begin:2026-07-23 by BestHui

#include <DX12Library/RootSignature.h>

#include <d3d12.h>
#include <d3dx12.h>
#include <wrl.h>

#include <memory>

class ComputePipelineStateBuilder final
{
public:
    explicit ComputePipelineStateBuilder(std::shared_ptr<RootSignature> rootSignature);

    Microsoft::WRL::ComPtr<ID3D12PipelineState> Build(Microsoft::WRL::ComPtr<ID3D12Device2> device) const;

    ComputePipelineStateBuilder& WithShader(const Microsoft::WRL::ComPtr<ID3DBlob>& computeShader);

private:
    std::shared_ptr<RootSignature> m_RootSignature;
    Microsoft::WRL::ComPtr<ID3DBlob> m_ComputeShader;
};
//Modify End
