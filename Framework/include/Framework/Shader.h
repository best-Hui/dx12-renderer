#pragma once

#include <d3d12.h>
#include <d3dx12.h>

#include <wrl.h>
#include <DX12Library/CommandList.h>
#include <DX12Library/ShaderUtils.h>
#include <DX12Library/RenderTargetState.h>
#include "CommonRootSignature.h"

#include <string>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "ShaderResourceView.h"
#include "RasterPipelineStateBuilder.h"
#include "ShaderBlob.h"
#include "ShaderReflection.h"
//Modify Begin:2026-07-23 by BestHui
#include "UnorderedAccessView.h"
//Modify End

class Shader
{
public:
	explicit Shader(
		const std::shared_ptr<CommonRootSignature>& rootSignature,
		const ShaderBlob& vertexShaderPath, const ShaderBlob& pixelShaderPath,
//Modify Begin:2026-07-21 by BestHui
		const std::function<void(RasterPipelineStateBuilder&)> buildPipelineState = [](RasterPipelineStateBuilder&) {},
		bool collectMetadata = true
//Modify End
	);

	Shader(const Shader& other) = delete;
	Shader& operator=(const Shader& other) = delete;
	Shader(Shader&& other) = delete;
	Shader& operator=(Shader&& other) = delete;

	void Bind(CommandList& commandList);
	void Unbind(CommandList& commandList);

	template<typename T>
	void SetPipelineConstantBuffer(CommandList& commandList, const T& data)
	{
		m_RootSignature->SetPipelineConstantBuffer(commandList, data);
	}

	template<typename T>
	void SetModelConstantBuffer(CommandList& commandList, const T& data)
	{
		m_RootSignature->SetMaterialConstantBuffer(commandList, data);
	}

	void SetMaterialConstantBuffer(CommandList& commandList, size_t size, const void* data);
	void SetConstantBuffer(CommandList& commandList, const std::string& variableName, size_t size, const void* data);

	template<typename T>
	void SetConstantBuffer(CommandList& commandList, const std::string& variableName, const T& data)
	{
		SetConstantBuffer(commandList, variableName, sizeof(T), &data);
	}

	void SetShaderResourceView(CommandList& commandList, const std::string& variableName, const ShaderResourceView& shaderResourceView);
	void SetTexture(CommandList& commandList, const std::string& variableName, const ShaderResourceView& shaderResourceView);
	void SetTexture(CommandList& commandList, const std::string& variableName, const std::shared_ptr<Resource>& texture);
//Modify Begin:2026-07-23 by BestHui
	void SetUnorderedAccessView(CommandList& commandList, const std::string& variableName, const UnorderedAccessView& unorderedAccessView);
//Modify End

	using ShaderMetadata = ShaderReflectionMetadata;

	const ShaderMetadata& GetVertexShaderMetadata() const { return m_VertexShaderMetadata; }
	const ShaderMetadata& GetPixelShaderMetadata() const { return m_PixelShaderMetadata; }

private:


	Microsoft::WRL::ComPtr<ID3D12PipelineState> GetPipelineState(const Microsoft::WRL::ComPtr<ID3D12Device2>& device, const RenderTargetState& renderTargetState);

	void CollectShaderMetadata(const Microsoft::WRL::ComPtr<ID3DBlob>& shader, ShaderMetadata* outMetadata);

	std::shared_ptr<CommonRootSignature> m_RootSignature;

	ShaderMetadata m_VertexShaderMetadata;
	ShaderMetadata m_PixelShaderMetadata;

	RasterPipelineStateBuilder m_PipelineStateBuilder;
	std::unordered_map<RenderTargetState, Microsoft::WRL::ComPtr<ID3D12PipelineState>> m_PipelineStateObjects;
};
