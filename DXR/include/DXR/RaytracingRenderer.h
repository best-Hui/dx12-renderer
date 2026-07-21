#pragma once

#include <DXR/RaytracingPipeline.h>
#include <DXR/RaytracingScene.h>
#include <DXR/RaytracingTypes.h>
#include <DXR/ShaderTable.h>

#include <DX12Library/StructuredBuffer.h>
#include <DX12Library/Texture.h>

#include <DirectXMath.h>

#include <memory>
#include <vector>

class CommandList;

namespace Dxr
{
    struct CameraConstants
    {
        DirectX::XMMATRIX InverseView = DirectX::XMMatrixIdentity();
        DirectX::XMMATRIX InverseProjection = DirectX::XMMatrixIdentity();
        DirectX::XMFLOAT4 CameraPosition = { 0.0f, 0.0f, 0.0f, 1.0f };
        uint32_t Width = 1;
        uint32_t Height = 1;
        uint32_t Padding0 = 0;
        uint32_t Padding1 = 0;
    };

    class RaytracingRenderer
    {
    public:
        explicit RaytracingRenderer(const ShaderBlob& shaderLibrary);

        void Resize(uint32_t width, uint32_t height);
        void SetMaterials(CommandList& commandList, const std::vector<MaterialData>& materials, const std::vector<std::shared_ptr<Texture>>& textures);
        void SetScene(CommandList& commandList, const RaytracingScene& scene);
        void Render(CommandList& commandList, const RaytracingScene& scene, const CameraConstants& camera);

        const std::shared_ptr<Texture>& GetOutputTexture() const;

    private:
        void BuildShaderTables();
        void RebuildDescriptorHeap(const RaytracingScene& scene);
        D3D12_GPU_DESCRIPTOR_HANDLE GetGpuDescriptorHandle(uint32_t descriptorIndex) const;
        D3D12_CPU_DESCRIPTOR_HANDLE GetCpuDescriptorHandle(uint32_t descriptorIndex) const;

        RaytracingPipeline m_Pipeline;
        ShaderTable m_RayGenerationShaderTable;
        ShaderTable m_MissShaderTable;
        ShaderTable m_HitGroupShaderTable;

        std::shared_ptr<Texture> m_OutputTexture;
        uint32_t m_Width = 1;
        uint32_t m_Height = 1;

        StructuredBuffer m_MaterialBuffer;
        StructuredBuffer m_GeometryBuffer;
        std::vector<std::shared_ptr<Texture>> m_Textures;

        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_DescriptorHeap;
        uint32_t m_DescriptorSize = 0;
        uint32_t m_VertexBuffersDescriptorStart = 0;
        uint32_t m_IndexBuffersDescriptorStart = 0;
        uint32_t m_TexturesDescriptorStart = 0;
        bool m_DescriptorsDirty = true;
    };
}
