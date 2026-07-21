#pragma once

#include <DX12Library/RootSignature.h>
#include <Framework/ShaderBlob.h>

#include <d3d12.h>
#include <wrl.h>

#include <memory>

namespace Dxr
{
    class RaytracingPipeline
    {
    public:
        enum RootParameters
        {
            Output,
            AccelerationStructure,
            Camera,
            Materials,
            Geometries,
            VertexBuffers,
            IndexBuffers,
            Textures,
            Count
        };

        RaytracingPipeline(const ShaderBlob& shaderLibrary, uint32_t maxDescriptorCount = 2048);

        RootSignature& GetGlobalRootSignature();
        const RootSignature& GetGlobalRootSignature() const;
        const Microsoft::WRL::ComPtr<ID3D12StateObject>& GetStateObject() const;

        const void* GetRayGenerationShaderIdentifier() const;
        const void* GetMissShaderIdentifier() const;
        const void* GetHitGroupShaderIdentifier() const;

        static bool IsSupported();

    private:
        void CreateGlobalRootSignature(uint32_t maxDescriptorCount);
        void CreateStateObject(const ShaderBlob& shaderLibrary);

        std::shared_ptr<RootSignature> m_GlobalRootSignature;
        Microsoft::WRL::ComPtr<ID3D12StateObject> m_StateObject;
        Microsoft::WRL::ComPtr<ID3D12StateObjectProperties> m_StateObjectProperties;
    };
}
