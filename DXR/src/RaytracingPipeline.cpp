#include <DXR/RaytracingPipeline.h>

#include <DX12Library/Application.h>
#include <DX12Library/Helpers.h>

#include <d3dx12.h>
#include <dxgi1_6.h>

using Microsoft::WRL::ComPtr;

namespace
{
    constexpr LPCWSTR RayGenerationShaderName = L"RayGen";
    constexpr LPCWSTR MissShaderName = L"Miss";
    constexpr LPCWSTR ClosestHitShaderName = L"ClosestHit";
    constexpr LPCWSTR HitGroupName = L"HitGroup";

    ComPtr<ID3D12Device5> GetDxrDevice()
    {
        ComPtr<ID3D12Device5> device5;
        ThrowIfFailed(Application::Get().GetDevice().As(&device5));
        return device5;
    }
}

namespace Dxr
{
    RaytracingPipeline::RaytracingPipeline(const ShaderBlob& shaderLibrary, const uint32_t maxDescriptorCount)
    {
        CreateGlobalRootSignature(maxDescriptorCount);
        CreateStateObject(shaderLibrary);
    }

    RootSignature& RaytracingPipeline::GetGlobalRootSignature()
    {
        return *m_GlobalRootSignature;
    }

    const RootSignature& RaytracingPipeline::GetGlobalRootSignature() const
    {
        return *m_GlobalRootSignature;
    }

    const ComPtr<ID3D12StateObject>& RaytracingPipeline::GetStateObject() const
    {
        return m_StateObject;
    }

    const void* RaytracingPipeline::GetRayGenerationShaderIdentifier() const
    {
        return m_StateObjectProperties->GetShaderIdentifier(RayGenerationShaderName);
    }

    const void* RaytracingPipeline::GetMissShaderIdentifier() const
    {
        return m_StateObjectProperties->GetShaderIdentifier(MissShaderName);
    }

    const void* RaytracingPipeline::GetHitGroupShaderIdentifier() const
    {
        return m_StateObjectProperties->GetShaderIdentifier(HitGroupName);
    }

    bool RaytracingPipeline::IsSupported()
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
        if (FAILED(Application::Get().GetDevice()->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5))))
        {
            return false;
        }

        return options5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
    }

    void RaytracingPipeline::CreateGlobalRootSignature(const uint32_t maxDescriptorCount)
    {
        using RootParameter = CD3DX12_ROOT_PARAMETER1;
        using DescriptorRange = CD3DX12_DESCRIPTOR_RANGE1;
        using StaticSampler = CD3DX12_STATIC_SAMPLER_DESC;

        DescriptorRange outputRange;
        outputRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0);

        DescriptorRange vertexBufferRange;
        vertexBufferRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, maxDescriptorCount, 0, 1);

        DescriptorRange indexBufferRange;
        indexBufferRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, maxDescriptorCount, 0, 2);

        DescriptorRange textureRange;
        textureRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, maxDescriptorCount, 0, 3);

        RootParameter rootParameters[RootParameters::Count];
        rootParameters[RootParameters::Output].InitAsDescriptorTable(1, &outputRange);
        rootParameters[RootParameters::AccelerationStructure].InitAsShaderResourceView(0, 0);
        rootParameters[RootParameters::Camera].InitAsConstantBufferView(0, 0);
        rootParameters[RootParameters::Materials].InitAsShaderResourceView(1, 0);
        rootParameters[RootParameters::Geometries].InitAsShaderResourceView(2, 0);
        rootParameters[RootParameters::VertexBuffers].InitAsDescriptorTable(1, &vertexBufferRange);
        rootParameters[RootParameters::IndexBuffers].InitAsDescriptorTable(1, &indexBufferRange);
        rootParameters[RootParameters::Textures].InitAsDescriptorTable(1, &textureRange);

        StaticSampler staticSampler(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);
        staticSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        staticSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;

        D3D12_ROOT_SIGNATURE_DESC1 desc1 = {};
        desc1.NumParameters = RootParameters::Count;
        desc1.pParameters = rootParameters;
        desc1.NumStaticSamplers = 1;
        desc1.pStaticSamplers = &staticSampler;
        desc1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        m_GlobalRootSignature = std::make_shared<RootSignature>(desc1, D3D_ROOT_SIGNATURE_VERSION_1_1);
    }

    void RaytracingPipeline::CreateStateObject(const ShaderBlob& shaderLibrary)
    {
        const auto device = GetDxrDevice();

        CD3DX12_STATE_OBJECT_DESC stateObjectDesc(D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);

        auto dxilLibrary = stateObjectDesc.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
        D3D12_SHADER_BYTECODE libraryBytecode = {};
        libraryBytecode.pShaderBytecode = shaderLibrary.GetBlob()->GetBufferPointer();
        libraryBytecode.BytecodeLength = shaderLibrary.GetBlob()->GetBufferSize();
        dxilLibrary->SetDXILLibrary(&libraryBytecode);
        dxilLibrary->DefineExport(RayGenerationShaderName);
        dxilLibrary->DefineExport(MissShaderName);
        dxilLibrary->DefineExport(ClosestHitShaderName);

        auto hitGroup = stateObjectDesc.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
        hitGroup->SetHitGroupExport(HitGroupName);
        hitGroup->SetClosestHitShaderImport(ClosestHitShaderName);
        hitGroup->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);

        auto shaderConfig = stateObjectDesc.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
        shaderConfig->Config(sizeof(float) * 4, sizeof(float) * 2);

        auto globalRootSignature = stateObjectDesc.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
        globalRootSignature->SetRootSignature(m_GlobalRootSignature->GetRootSignature().Get());

        auto pipelineConfig = stateObjectDesc.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
        pipelineConfig->Config(1);

        ThrowIfFailed(device->CreateStateObject(stateObjectDesc, IID_PPV_ARGS(&m_StateObject)));
        ThrowIfFailed(m_StateObject.As(&m_StateObjectProperties));
    }
}
