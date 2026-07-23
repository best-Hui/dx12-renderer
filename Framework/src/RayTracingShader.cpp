//Modify Begin:2026-07-21 by BestHui
#include <Framework/RayTracingShader.h>

#include <DX12Library/Application.h>
#include <DX12Library/CommandList.h>
#include <DX12Library/DescriptorAllocation.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/ShaderUtils.h>
#include <DX12Library/StructuredBuffer.h>
#include <DX12Library/Texture.h>
#include <Framework/Mesh.h>
#include <Framework/RayTracingPipelineStateBuilder.h>
#include <Framework/ShaderBlob.h>

#include <d3dx12.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <unordered_map>

#if defined(min)
#undef min
#endif

#if defined(max)
#undef max
#endif

using Microsoft::WRL::ComPtr;

namespace
{
    constexpr LPCWSTR DefaultRayGenerationShaderName = L"RayGen";
    constexpr LPCWSTR DefaultMissShaderName = L"Miss";
    constexpr LPCWSTR DefaultClosestHitShaderName = L"ClosestHit";
    constexpr LPCWSTR DefaultHitGroupName = L"HitGroup";

    bool IsDescriptorTableBinding(const RayTracingShaderBindingType type)
    {
        switch (type)
        {
        case RayTracingShaderBindingType::OutputTexture:
        case RayTracingShaderBindingType::TextureArray:
        case RayTracingShaderBindingType::VertexBufferArray:
        case RayTracingShaderBindingType::IndexBufferArray:
            return true;
        default:
            return false;
        }
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC CreateVertexBufferSrvDesc(const VertexBuffer& vertexBuffer)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
        desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.Buffer.NumElements = static_cast<UINT>(vertexBuffer.GetNumVertices());
        desc.Buffer.StructureByteStride = static_cast<UINT>(vertexBuffer.GetVertexStride());
        desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        return desc;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC CreateIndexBufferSrvDesc(const IndexBuffer& indexBuffer)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
        desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        desc.Format = indexBuffer.GetIndexFormat();
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.Buffer.NumElements = static_cast<UINT>(indexBuffer.GetNumIndices());
        desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        return desc;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC CreateNullVertexBufferSrvDesc()
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
        desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.Buffer.NumElements = 1;
        desc.Buffer.StructureByteStride = sizeof(VertexAttributes);
        desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        return desc;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC CreateNullIndexBufferSrvDesc()
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
        desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        desc.Format = DXGI_FORMAT_R32_UINT;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.Buffer.NumElements = 1;
        desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        return desc;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC CreateNullTextureSrvDesc()
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.Texture2D.MipLevels = 1;
        return desc;
    }

    uint32_t NormalizeDescriptorCount(const uint32_t descriptorCount, const uint32_t maxDescriptorCount)
    {
        return descriptorCount == 0u || descriptorCount == UINT32_MAX ? maxDescriptorCount : descriptorCount;
    }

    bool IsBufferSrvDimension(const D3D_SRV_DIMENSION dimension)
    {
        return dimension == D3D_SRV_DIMENSION_BUFFER;
    }

    bool IsRayTracingAccelerationStructureSrv(const ShaderUtils::ShaderResourceViewMetadata& srv)
    {
        constexpr int RayTracingAccelerationStructureSrvDimension = 11;
        return srv.InputType == D3D_SIT_RTACCELERATIONSTRUCTURE ||
            static_cast<int>(srv.Dimension) == RayTracingAccelerationStructureSrvDimension ||
            srv.Name == "Scene";
    }

    const char* GetRayTracingBindingTypeName(const RayTracingShaderBindingType type)
    {
        switch (type)
        {
        case RayTracingShaderBindingType::OutputTexture:
            return "OutputTexture";
        case RayTracingShaderBindingType::AccelerationStructure:
            return "AccelerationStructure";
        case RayTracingShaderBindingType::ConstantBuffer:
            return "ConstantBuffer";
        case RayTracingShaderBindingType::StructuredBuffer:
            return "StructuredBuffer";
        case RayTracingShaderBindingType::TextureArray:
            return "TextureArray";
        case RayTracingShaderBindingType::VertexBufferArray:
            return "VertexBufferArray";
        case RayTracingShaderBindingType::IndexBufferArray:
            return "IndexBufferArray";
        default:
            return "Unknown";
        }
    }

    struct ShaderRecord
    {
        const void* ShaderIdentifier = nullptr;
        std::vector<uint8_t> LocalRootArguments;
    };

    class ShaderTable
    {
    public:
        void Reset(
            const ComPtr<ID3D12Device>& device,
            const std::vector<ShaderRecord>& shaderRecords,
            const uint32_t shaderIdentifierSize,
            const wchar_t* name)
        {
            uint32_t maxRecordSize = shaderIdentifierSize;
            for (const ShaderRecord& record : shaderRecords)
            {
                maxRecordSize = std::max<uint32_t>(
                    maxRecordSize,
                    shaderIdentifierSize + static_cast<uint32_t>(record.LocalRootArguments.size()));
            }

            m_StrideInBytes = Math::AlignUp(maxRecordSize, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
            m_SizeInBytes = static_cast<uint64_t>(m_StrideInBytes) * shaderRecords.size();

            const auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            const auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(m_SizeInBytes);
            ThrowIfFailed(device->CreateCommittedResource(
                &heapProperties,
                D3D12_HEAP_FLAG_NONE,
                &resourceDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&m_Resource)));

            if (name != nullptr)
            {
                m_Resource->SetName(name);
            }

            uint8_t* mappedData = nullptr;
            ThrowIfFailed(m_Resource->Map(0, nullptr, reinterpret_cast<void**>(&mappedData)));

            for (size_t i = 0; i < shaderRecords.size(); ++i)
            {
                const ShaderRecord& record = shaderRecords[i];
                uint8_t* recordData = mappedData + i * m_StrideInBytes;
                std::memcpy(recordData, record.ShaderIdentifier, shaderIdentifierSize);
                if (!record.LocalRootArguments.empty())
                {
                    std::memcpy(recordData + shaderIdentifierSize, record.LocalRootArguments.data(), record.LocalRootArguments.size());
                }
            }

            m_Resource->Unmap(0, nullptr);
        }

        D3D12_GPU_VIRTUAL_ADDRESS GetGpuVirtualAddress() const
        {
            return m_Resource->GetGPUVirtualAddress();
        }

        uint64_t GetSizeInBytes() const
        {
            return m_SizeInBytes;
        }

        uint32_t GetStrideInBytes() const
        {
            return m_StrideInBytes;
        }

    private:
        ComPtr<ID3D12Resource> m_Resource;
        uint64_t m_SizeInBytes = 0;
        uint32_t m_StrideInBytes = 0;
    };

}

struct RayTracingShader::Impl
{
    Impl(const ShaderBlob& shaderLibrary, RayTracingPipelineDesc pipelineDesc)
        : Desc(std::move(pipelineDesc))
        , PipelineState(RayTracingPipelineStateBuilder(shaderLibrary, Desc).Build())
    {
        Assert(!Desc.Bindings.empty(), "Ray tracing shader requires at least one binding.");
        for (uint32_t i = 0; i < Desc.Bindings.size(); ++i)
        {
            const RayTracingShaderBindingDesc& binding = Desc.Bindings[i];
            Assert(!binding.Name.empty(), "Ray tracing binding name must not be empty.");
            BindingIndicesByName.emplace(binding.Name, i);
        }
    }

    const RayTracingShaderBindingDesc& GetBinding(std::string_view name, RayTracingShaderBindingType expectedType) const
    {
        const auto findResult = BindingIndicesByName.find(std::string(name));
        Assert(findResult != BindingIndicesByName.end(), "Ray tracing shader binding was not found.");

        const RayTracingShaderBindingDesc& binding = Desc.Bindings[findResult->second];
        if (binding.Type != expectedType)
        {
            const std::string message =
                "Ray tracing shader binding type does not match the setter. Name=" +
                std::string(name) +
                " Expected=" +
                GetRayTracingBindingTypeName(expectedType) +
                " Actual=" +
                GetRayTracingBindingTypeName(binding.Type);
            throw std::exception(message.c_str());
        }
        return binding;
    }

    uint32_t GetBindingIndex(const RayTracingShaderBindingDesc& binding) const
    {
        return static_cast<uint32_t>(&binding - Desc.Bindings.data());
    }

    void MarkDescriptorsDirty(const RayTracingShaderBindingDesc& binding)
    {
        if (IsDescriptorTableBinding(binding.Type))
        {
            DescriptorsDirty = true;
        }
    }

    const RayTracingShaderPassDesc& ResolvePass(std::string_view passName)
    {
        if (Desc.Passes.empty())
        {
            DefaultPass.Name = std::string(passName);
            DefaultPass.RayGenerationShader = std::wstring(passName.begin(), passName.end());
            DefaultPass.MissShaderRecords = { { DefaultMissShaderName, {} } };
            DefaultPass.HitGroupRecords = { { DefaultHitGroupName, {} } };
            return DefaultPass;
        }

        const auto passFindResult = std::find_if(
            Desc.Passes.begin(),
            Desc.Passes.end(),
            [passName](const RayTracingShaderPassDesc& pass)
            {
                return pass.Name == passName;
            });

        Assert(passFindResult != Desc.Passes.end(), "Ray tracing shader pass was not found.");
        return *passFindResult;
    }

    std::vector<ShaderRecord> BuildShaderRecords(const std::vector<RayTracingShaderRecordDesc>& recordDescs)
    {
        std::vector<ShaderRecord> records;
        records.reserve(recordDescs.size());
        for (const RayTracingShaderRecordDesc& recordDesc : recordDescs)
        {
            const void* shaderIdentifier = PipelineState->GetShaderIdentifier(recordDesc.ExportName);
            Assert(shaderIdentifier != nullptr, "Ray tracing shader identifier was not found.");
            records.push_back({ shaderIdentifier, recordDesc.LocalRootArguments });
        }
        return records;
    }

    void BuildShaderTables(const RayTracingShaderPassDesc& pass)
    {
        if (CurrentPassName == pass.Name)
        {
            return;
        }

        const auto device = Application::Get().GetDevice();

        RayGenerationShaderTable.Reset(
            device,
            BuildShaderRecords({ { pass.RayGenerationShader, {} } }),
            D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES,
            L"Ray Generation Shader Table");
        MissShaderTable.Reset(
            device,
            BuildShaderRecords(pass.MissShaderRecords),
            D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES,
            L"Miss Shader Table");
        HitGroupShaderTable.Reset(
            device,
            BuildShaderRecords(pass.HitGroupRecords),
            D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES,
            L"Hit Group Shader Table");

        CurrentPassName = pass.Name;
    }

    void RebuildDescriptorAllocation()
    {
        const auto device = Application::Get().GetDevice();

        uint32_t descriptorCount = 0;
        DescriptorTableOffsets.clear();
        DescriptorTableOffsets.resize(Desc.Bindings.size(), UINT32_MAX);
        for (uint32_t i = 0; i < Desc.Bindings.size(); ++i)
        {
            const RayTracingShaderBindingDesc& binding = Desc.Bindings[i];
            if (IsDescriptorTableBinding(binding.Type))
            {
                DescriptorTableOffsets[i] = descriptorCount;
                descriptorCount += std::max(1u, binding.DescriptorCount);
            }
        }

        const uint32_t requiredDescriptorCount = std::max(1u, descriptorCount);
        if (DescriptorAllocation.IsNull() || DescriptorAllocation.GetNumHandles() < requiredDescriptorCount)
        {
            DescriptorAllocation = Application::Get().AllocateDescriptors(
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                requiredDescriptorCount);
        }

        const D3D12_SHADER_RESOURCE_VIEW_DESC nullVertexSrvDesc = CreateNullVertexBufferSrvDesc();
        const D3D12_SHADER_RESOURCE_VIEW_DESC nullIndexSrvDesc = CreateNullIndexBufferSrvDesc();
        const D3D12_SHADER_RESOURCE_VIEW_DESC nullTextureSrvDesc = CreateNullTextureSrvDesc();

        for (uint32_t bindingIndex = 0; bindingIndex < Desc.Bindings.size(); ++bindingIndex)
        {
            const RayTracingShaderBindingDesc& binding = Desc.Bindings[bindingIndex];
            if (!IsDescriptorTableBinding(binding.Type))
            {
                continue;
            }

            const uint32_t descriptorOffset = DescriptorTableOffsets[bindingIndex];
            const BoundBinding& boundBinding = BoundBindings[bindingIndex];

            if (binding.Type == RayTracingShaderBindingType::OutputTexture)
            {
                Assert(boundBinding.TextureResource != nullptr, "Ray tracing output texture is not bound.");
                device->CopyDescriptorsSimple(
                    1,
                    DescriptorAllocation.GetDescriptorHandle(descriptorOffset),
                    boundBinding.TextureResource->GetUnorderedAccessView(),
                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                continue;
            }

            if (binding.Type == RayTracingShaderBindingType::TextureArray)
            {
                Assert(boundBinding.TextureShaderResourceViews.size() <= binding.DescriptorCount, "Ray tracing texture array exceeds binding descriptor count.");
                for (uint32_t i = 0; i < binding.DescriptorCount; ++i)
                {
                    if (i < boundBinding.TextureShaderResourceViews.size())
                    {
                        const ShaderResourceView& shaderResourceView = boundBinding.TextureShaderResourceViews[i];
                        device->CopyDescriptorsSimple(
                            1,
                            DescriptorAllocation.GetDescriptorHandle(descriptorOffset + i),
                            shaderResourceView.m_Resource->GetShaderResourceView(shaderResourceView.GetDescOrNullptr()),
                            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                    }
                    else
                    {
                        device->CreateShaderResourceView(nullptr, &nullTextureSrvDesc, DescriptorAllocation.GetDescriptorHandle(descriptorOffset + i));
                    }
                }
                continue;
            }

            Assert(AccelerationStructure != nullptr, "Ray tracing acceleration structure is not bound.");
            const std::vector<std::shared_ptr<Mesh>>& meshes = AccelerationStructure->GetMeshes();
            Assert(meshes.size() <= binding.DescriptorCount, "Ray tracing mesh descriptor array exceeds binding descriptor count.");

            for (uint32_t i = 0; i < binding.DescriptorCount; ++i)
            {
                if (i >= meshes.size())
                {
                    const auto& nullDesc = binding.Type == RayTracingShaderBindingType::VertexBufferArray ?
                        nullVertexSrvDesc :
                        nullIndexSrvDesc;
                    device->CreateShaderResourceView(nullptr, &nullDesc, DescriptorAllocation.GetDescriptorHandle(descriptorOffset + i));
                    continue;
                }

                const auto& mesh = meshes[i];
                if (binding.Type == RayTracingShaderBindingType::VertexBufferArray)
                {
                    const VertexBuffer& vertexBuffer = mesh->GetVertexBuffer();
                    const D3D12_SHADER_RESOURCE_VIEW_DESC vertexSrvDesc = CreateVertexBufferSrvDesc(vertexBuffer);
                    device->CreateShaderResourceView(
                        vertexBuffer.GetD3D12Resource().Get(),
                        &vertexSrvDesc,
                        DescriptorAllocation.GetDescriptorHandle(descriptorOffset + i));
                }
                else
                {
                    const IndexBuffer& indexBuffer = mesh->GetIndexBuffer();
                    const D3D12_SHADER_RESOURCE_VIEW_DESC indexSrvDesc = CreateIndexBufferSrvDesc(indexBuffer);
                    device->CreateShaderResourceView(
                        indexBuffer.GetD3D12Resource().Get(),
                        &indexSrvDesc,
                        DescriptorAllocation.GetDescriptorHandle(descriptorOffset + i));
                }
            }
        }
    }

    struct BoundBinding
    {
        std::shared_ptr<Texture> TextureResource;
        std::vector<ShaderResourceView> TextureShaderResourceViews;
        const StructuredBuffer* Buffer = nullptr;
        std::vector<uint8_t> ConstantBufferData;
    };

    RayTracingPipelineDesc Desc;
    std::shared_ptr<RayTracingPipelineState> PipelineState;
    ShaderTable RayGenerationShaderTable;
    ShaderTable MissShaderTable;
    ShaderTable HitGroupShaderTable;
    RayTracingShaderPassDesc DefaultPass;
    std::string CurrentPassName;

    std::unordered_map<std::string, uint32_t> BindingIndicesByName;
    std::map<uint32_t, BoundBinding> BoundBindings;
    const RayTracingAccelerationStructure* AccelerationStructure = nullptr;

    DescriptorAllocation DescriptorAllocation;
    std::vector<uint32_t> DescriptorTableOffsets;
    bool DescriptorsDirty = true;
};

RayTracingPipelineDesc RayTracingShader::CreateDefaultPipelineDesc()
{
    RayTracingPipelineDesc desc;
    desc.Exports = {
        DefaultRayGenerationShaderName,
        DefaultMissShaderName,
        DefaultClosestHitShaderName
    };
    desc.HitGroups = {
        {
            DefaultHitGroupName,
            DefaultClosestHitShaderName,
            L"",
            L"",
            D3D12_HIT_GROUP_TYPE_TRIANGLES
        }
    };
    desc.Bindings = {
        { "Output", RayTracingShaderBindingType::OutputTexture, 0, 0, 1 },
        { "Scene", RayTracingShaderBindingType::AccelerationStructure, 0, 0, 1 },
        { "CameraConstants", RayTracingShaderBindingType::ConstantBuffer, 0, 0, 1 },
        { "Materials", RayTracingShaderBindingType::StructuredBuffer, 1, 0, 1 },
        { "Geometries", RayTracingShaderBindingType::StructuredBuffer, 2, 0, 1 },
        { "VertexBuffers", RayTracingShaderBindingType::VertexBufferArray, 0, 1, desc.MaxDescriptorCount },
        { "IndexBuffers", RayTracingShaderBindingType::IndexBufferArray, 0, 2, desc.MaxDescriptorCount },
        { "Textures", RayTracingShaderBindingType::TextureArray, 0, 3, desc.MaxDescriptorCount }
    };
    desc.Passes = {
        {
            "RayGen",
            DefaultRayGenerationShaderName,
            { { DefaultMissShaderName, {} } },
            { { DefaultHitGroupName, {} } }
        }
    };
    return desc;
}

//Modify Begin:2026-07-23 by BestHui
RayTracingPipelineDescBuilder::RayTracingPipelineDescBuilder() = default;

RayTracingPipelineDescBuilder::RayTracingPipelineDescBuilder(RayTracingPipelineDesc desc)
    : m_Desc(std::move(desc))
{
}

RayTracingPipelineDescBuilder RayTracingPipelineDescBuilder::Default()
{
    return RayTracingPipelineDescBuilder(RayTracingShader::CreateDefaultPipelineDesc());
}

RayTracingPipelineDescBuilder RayTracingPipelineDescBuilder::ReflectedDefault(const ShaderBlob& shaderLibrary)
{
    RayTracingPipelineDesc desc = RayTracingShader::CreateDefaultPipelineDesc();
    desc.Bindings.clear();

    const auto reflection = ShaderUtils::ReflectLibrary(shaderLibrary.GetBlob());

    for (const auto& cbuffer : ShaderUtils::GetConstantBuffers(reflection))
    {
        desc.Bindings.push_back({
            cbuffer.Name,
            RayTracingShaderBindingType::ConstantBuffer,
            cbuffer.RegisterIndex,
            cbuffer.Space,
            1
        });
    }

    for (const auto& srv : ShaderUtils::GetShaderResourceViews(reflection))
    {
        RayTracingShaderBindingType bindingType = RayTracingShaderBindingType::StructuredBuffer;
        const uint32_t descriptorCount = NormalizeDescriptorCount(srv.BindCount, desc.MaxDescriptorCount);

        if (IsRayTracingAccelerationStructureSrv(srv))
        {
            bindingType = RayTracingShaderBindingType::AccelerationStructure;
        }
        else if (srv.Name == "VertexBuffers")
        {
            bindingType = RayTracingShaderBindingType::VertexBufferArray;
        }
        else if (srv.Name == "IndexBuffers")
        {
            bindingType = RayTracingShaderBindingType::IndexBufferArray;
        }
        else if (srv.InputType == D3D_SIT_TEXTURE || !IsBufferSrvDimension(srv.Dimension) || descriptorCount > 1u)
        {
            bindingType = RayTracingShaderBindingType::TextureArray;
        }

        desc.Bindings.push_back({
            srv.Name,
            bindingType,
            srv.RegisterIndex,
            srv.Space,
            descriptorCount
        });
    }

    for (const auto& uav : ShaderUtils::GetUnorderedAccessViews(reflection))
    {
        desc.Bindings.push_back({
            uav.Name,
            RayTracingShaderBindingType::OutputTexture,
            uav.RegisterIndex,
            uav.Space,
            NormalizeDescriptorCount(uav.BindCount, desc.MaxDescriptorCount)
        });
    }

    return RayTracingPipelineDescBuilder(std::move(desc));
}

RayTracingPipelineDescBuilder& RayTracingPipelineDescBuilder::WithExport(std::wstring exportName)
{
    m_Desc.Exports.push_back(std::move(exportName));
    return *this;
}

RayTracingPipelineDescBuilder& RayTracingPipelineDescBuilder::WithTriangleHitGroup(
    std::wstring hitGroupName,
    std::wstring closestHitShader,
    std::wstring anyHitShader,
    std::wstring intersectionShader)
{
    m_Desc.HitGroups.push_back({
        std::move(hitGroupName),
        std::move(closestHitShader),
        std::move(anyHitShader),
        std::move(intersectionShader),
        D3D12_HIT_GROUP_TYPE_TRIANGLES
    });
    return *this;
}

RayTracingPipelineDescBuilder& RayTracingPipelineDescBuilder::WithRayGenerationPass(
    std::string passName,
    std::wstring rayGenerationShader,
    std::vector<std::wstring> missShaders,
    std::vector<std::wstring> hitGroups)
{
    RayTracingShaderPassDesc passDesc;
    passDesc.Name = std::move(passName);
    passDesc.RayGenerationShader = std::move(rayGenerationShader);
    passDesc.MissShaderRecords.reserve(missShaders.size());
    passDesc.HitGroupRecords.reserve(hitGroups.size());

    for (std::wstring& missShader : missShaders)
    {
        passDesc.MissShaderRecords.push_back({ std::move(missShader), {} });
    }

    for (std::wstring& hitGroup : hitGroups)
    {
        passDesc.HitGroupRecords.push_back({ std::move(hitGroup), {} });
    }

    m_Desc.Passes.push_back(std::move(passDesc));
    return *this;
}

RayTracingPipelineDescBuilder& RayTracingPipelineDescBuilder::WithOutputTexture(
    std::string name,
    const uint32_t shaderRegister,
    const uint32_t registerSpace,
    const uint32_t descriptorCount)
{
    return WithBinding(std::move(name), RayTracingShaderBindingType::OutputTexture, shaderRegister, registerSpace, descriptorCount);
}

RayTracingPipelineDescBuilder& RayTracingPipelineDescBuilder::WithAccelerationStructure(
    std::string name,
    const uint32_t shaderRegister,
    const uint32_t registerSpace)
{
    return WithBinding(std::move(name), RayTracingShaderBindingType::AccelerationStructure, shaderRegister, registerSpace, 1);
}

RayTracingPipelineDescBuilder& RayTracingPipelineDescBuilder::WithConstantBuffer(
    std::string name,
    const uint32_t shaderRegister,
    const uint32_t registerSpace)
{
    return WithBinding(std::move(name), RayTracingShaderBindingType::ConstantBuffer, shaderRegister, registerSpace, 1);
}

RayTracingPipelineDescBuilder& RayTracingPipelineDescBuilder::WithStructuredBuffer(
    std::string name,
    const uint32_t shaderRegister,
    const uint32_t registerSpace)
{
    return WithBinding(std::move(name), RayTracingShaderBindingType::StructuredBuffer, shaderRegister, registerSpace, 1);
}

RayTracingPipelineDescBuilder& RayTracingPipelineDescBuilder::WithTextureArray(
    std::string name,
    const uint32_t shaderRegister,
    const uint32_t registerSpace,
    const uint32_t descriptorCount)
{
    return WithBinding(std::move(name), RayTracingShaderBindingType::TextureArray, shaderRegister, registerSpace, descriptorCount);
}

RayTracingPipelineDescBuilder& RayTracingPipelineDescBuilder::WithVertexBufferArray(
    std::string name,
    const uint32_t shaderRegister,
    const uint32_t registerSpace,
    const uint32_t descriptorCount)
{
    return WithBinding(std::move(name), RayTracingShaderBindingType::VertexBufferArray, shaderRegister, registerSpace, descriptorCount);
}

RayTracingPipelineDescBuilder& RayTracingPipelineDescBuilder::WithIndexBufferArray(
    std::string name,
    const uint32_t shaderRegister,
    const uint32_t registerSpace,
    const uint32_t descriptorCount)
{
    return WithBinding(std::move(name), RayTracingShaderBindingType::IndexBufferArray, shaderRegister, registerSpace, descriptorCount);
}

RayTracingPipelineDescBuilder& RayTracingPipelineDescBuilder::WithPayloadSize(const uint32_t payloadSizeInBytes)
{
    m_Desc.PayloadSizeInBytes = payloadSizeInBytes;
    return *this;
}

RayTracingPipelineDescBuilder& RayTracingPipelineDescBuilder::WithAttributeSize(const uint32_t attributeSizeInBytes)
{
    m_Desc.AttributeSizeInBytes = attributeSizeInBytes;
    return *this;
}

RayTracingPipelineDescBuilder& RayTracingPipelineDescBuilder::WithMaxRecursionDepth(const uint32_t maxTraceRecursionDepth)
{
    m_Desc.MaxTraceRecursionDepth = maxTraceRecursionDepth;
    return *this;
}

RayTracingPipelineDescBuilder& RayTracingPipelineDescBuilder::WithMaxDescriptorCount(const uint32_t maxDescriptorCount)
{
    m_Desc.MaxDescriptorCount = maxDescriptorCount;
    return *this;
}

RayTracingPipelineDesc RayTracingPipelineDescBuilder::Build() const
{
    return m_Desc;
}

RayTracingPipelineDescBuilder& RayTracingPipelineDescBuilder::WithBinding(
    std::string name,
    const RayTracingShaderBindingType type,
    const uint32_t shaderRegister,
    const uint32_t registerSpace,
    const uint32_t descriptorCount)
{
    m_Desc.Bindings.push_back({
        std::move(name),
        type,
        shaderRegister,
        registerSpace,
        descriptorCount
    });
    return *this;
}
//Modify End

RayTracingShader::RayTracingShader(const ShaderBlob& shaderLibrary)
    : RayTracingShader(shaderLibrary, CreateDefaultPipelineDesc())
{}

RayTracingShader::RayTracingShader(const ShaderBlob& shaderLibrary, RayTracingPipelineDesc desc)
    : m_Impl(std::make_unique<Impl>(shaderLibrary, std::move(desc)))
{}

RayTracingShader::~RayTracingShader() = default;
RayTracingShader::RayTracingShader(RayTracingShader&&) noexcept = default;
RayTracingShader& RayTracingShader::operator=(RayTracingShader&&) noexcept = default;

bool RayTracingShader::IsSupported()
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
    if (FAILED(Application::Get().GetDevice()->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5))))
    {
        return false;
    }

    return options5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
}

const RayTracingPipelineDesc& RayTracingShader::GetDesc() const
{
    return m_Impl->Desc;
}

void RayTracingShader::SetOutputTexture(std::string_view name, const std::shared_ptr<Texture>& texture)
{
    const RayTracingShaderBindingDesc& binding = m_Impl->GetBinding(name, RayTracingShaderBindingType::OutputTexture);
    Assert(texture != nullptr, "Ray tracing output texture must not be null.");
    m_Impl->BoundBindings[m_Impl->GetBindingIndex(binding)].TextureResource = texture;
    m_Impl->MarkDescriptorsDirty(binding);
}

void RayTracingShader::SetAccelerationStructure(std::string_view name, const RayTracingAccelerationStructure& accelerationStructure)
{
    m_Impl->GetBinding(name, RayTracingShaderBindingType::AccelerationStructure);
    m_Impl->AccelerationStructure = &accelerationStructure;
    m_Impl->DescriptorsDirty = true;
}

void RayTracingShader::SetConstantBufferData(std::string_view name, const void* data, const size_t size)
{
    const RayTracingShaderBindingDesc& binding = m_Impl->GetBinding(name, RayTracingShaderBindingType::ConstantBuffer);
    Assert(data != nullptr && size > 0, "Ray tracing constant buffer data must not be empty.");

    auto& constantBufferData = m_Impl->BoundBindings[m_Impl->GetBindingIndex(binding)].ConstantBufferData;
    constantBufferData.resize(size);
    std::memcpy(constantBufferData.data(), data, size);
}

void RayTracingShader::SetStructuredBuffer(std::string_view name, const StructuredBuffer& buffer)
{
    const RayTracingShaderBindingDesc& binding = m_Impl->GetBinding(name, RayTracingShaderBindingType::StructuredBuffer);
    m_Impl->BoundBindings[m_Impl->GetBindingIndex(binding)].Buffer = &buffer;
}

void RayTracingShader::SetTextureArray(std::string_view name, const std::vector<std::shared_ptr<Texture>>& textures)
{
    std::vector<ShaderResourceView> shaderResourceViews;
    shaderResourceViews.reserve(textures.size());
    for (const auto& texture : textures)
    {
        shaderResourceViews.emplace_back(texture);
    }
    SetTextureArray(name, shaderResourceViews);
}

void RayTracingShader::SetTextureArray(
    std::string_view name,
    const std::vector<std::shared_ptr<Texture>>& textures,
    const std::vector<D3D12_SHADER_RESOURCE_VIEW_DESC>& srvDescs)
{
    Assert(srvDescs.size() == textures.size(), "Ray tracing texture SRV desc count must match the texture count.");
    std::vector<ShaderResourceView> shaderResourceViews;
    shaderResourceViews.reserve(textures.size());
    for (size_t i = 0; i < textures.size(); ++i)
    {
        shaderResourceViews.emplace_back(textures[i], srvDescs[i]);
    }
    SetTextureArray(name, shaderResourceViews);
}

void RayTracingShader::SetTextureArray(std::string_view name, const std::vector<ShaderResourceView>& shaderResourceViews)
{
    const RayTracingShaderBindingDesc& binding = m_Impl->GetBinding(name, RayTracingShaderBindingType::TextureArray);
    Assert(!shaderResourceViews.empty(), "Ray tracing texture array must not be empty.");
    Assert(shaderResourceViews.size() <= binding.DescriptorCount, "Ray tracing texture array exceeds binding descriptor count.");
    for (const ShaderResourceView& shaderResourceView : shaderResourceViews)
    {
        Assert(shaderResourceView.m_Resource != nullptr, "Ray tracing texture SRV resource must not be null.");
    }

    auto& boundBinding = m_Impl->BoundBindings[m_Impl->GetBindingIndex(binding)];
    boundBinding.TextureShaderResourceViews = shaderResourceViews;
    m_Impl->MarkDescriptorsDirty(binding);
}

void RayTracingShader::Dispatch(
    CommandList& commandList,
    std::string_view passName,
    const uint32_t width,
    const uint32_t height,
    const uint32_t depth)
{
    const RayTracingShaderPassDesc& pass = m_Impl->ResolvePass(passName);
    Assert(!pass.RayGenerationShader.empty(), "Ray tracing pass requires a ray generation shader.");
    Assert(m_Impl->AccelerationStructure != nullptr, "Ray tracing acceleration structure is not bound.");

    m_Impl->BuildShaderTables(pass);

    if (m_Impl->DescriptorsDirty)
    {
        m_Impl->RebuildDescriptorAllocation();
        m_Impl->DescriptorsDirty = false;
    }

    for (uint32_t bindingIndex = 0; bindingIndex < m_Impl->Desc.Bindings.size(); ++bindingIndex)
    {
        const RayTracingShaderBindingDesc& binding = m_Impl->Desc.Bindings[bindingIndex];
        const auto boundBindingIt = m_Impl->BoundBindings.find(bindingIndex);
        const Impl::BoundBinding* boundBinding = boundBindingIt != m_Impl->BoundBindings.end() ? &boundBindingIt->second : nullptr;

        if (binding.Type == RayTracingShaderBindingType::OutputTexture)
        {
            Assert(boundBinding != nullptr && boundBinding->TextureResource != nullptr, "Ray tracing output texture is not bound.");
            if (boundBinding->TextureResource->AreAutoBarriersEnabled())
            {
                commandList.TransitionBarrier(*boundBinding->TextureResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            }
        }
        else if (binding.Type == RayTracingShaderBindingType::TextureArray)
        {
            Assert(boundBinding != nullptr, "Ray tracing texture array is not bound.");
            for (const ShaderResourceView& shaderResourceView : boundBinding->TextureShaderResourceViews)
            {
                if (shaderResourceView.m_Resource->AreAutoBarriersEnabled())
                {
                    commandList.TransitionBarrier(*shaderResourceView.m_Resource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                }
            }
        }
        else if (binding.Type == RayTracingShaderBindingType::StructuredBuffer)
        {
            Assert(boundBinding != nullptr && boundBinding->Buffer != nullptr, "Ray tracing structured buffer is not bound.");
            if (boundBinding->Buffer->AreAutoBarriersEnabled())
            {
                commandList.TransitionBarrier(*boundBinding->Buffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            }
        }
    }

    commandList.SetRaytracingPipelineState(m_Impl->PipelineState->GetStateObject());
    commandList.SetComputeRootSignature(m_Impl->PipelineState->GetGlobalRootSignature());

    for (uint32_t bindingIndex = 0; bindingIndex < m_Impl->Desc.Bindings.size(); ++bindingIndex)
    {
        const RayTracingShaderBindingDesc& binding = m_Impl->Desc.Bindings[bindingIndex];
        const auto boundBindingIt = m_Impl->BoundBindings.find(bindingIndex);
        const Impl::BoundBinding* boundBinding = boundBindingIt != m_Impl->BoundBindings.end() ? &boundBindingIt->second : nullptr;

        if (IsDescriptorTableBinding(binding.Type))
        {
            commandList.StageDynamicDescriptors(
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                bindingIndex,
                0,
                std::max(1u, binding.DescriptorCount),
                m_Impl->DescriptorAllocation.GetDescriptorHandle(m_Impl->DescriptorTableOffsets[bindingIndex]));
        }
        else if (binding.Type == RayTracingShaderBindingType::AccelerationStructure)
        {
            commandList.SetComputeRootShaderResourceView(bindingIndex, m_Impl->AccelerationStructure->GetGpuVirtualAddress());
        }
        else if (binding.Type == RayTracingShaderBindingType::ConstantBuffer)
        {
            Assert(boundBinding != nullptr && !boundBinding->ConstantBufferData.empty(), "Ray tracing constant buffer is not bound.");
            auto allocation = commandList.AllocateInUploadBuffer(
                boundBinding->ConstantBufferData.size(),
                D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
            std::memcpy(allocation.Cpu, boundBinding->ConstantBufferData.data(), boundBinding->ConstantBufferData.size());
            commandList.SetComputeRootConstantBufferView(bindingIndex, allocation.Gpu);
        }
        else if (binding.Type == RayTracingShaderBindingType::StructuredBuffer)
        {
            Assert(boundBinding != nullptr && boundBinding->Buffer != nullptr, "Ray tracing structured buffer is not bound.");
            commandList.SetComputeRootShaderResourceView(bindingIndex, boundBinding->Buffer->GetD3D12Resource()->GetGPUVirtualAddress());
        }
    }

    D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
    dispatchDesc.RayGenerationShaderRecord.StartAddress = m_Impl->RayGenerationShaderTable.GetGpuVirtualAddress();
    dispatchDesc.RayGenerationShaderRecord.SizeInBytes = m_Impl->RayGenerationShaderTable.GetSizeInBytes();
    dispatchDesc.MissShaderTable.StartAddress = m_Impl->MissShaderTable.GetGpuVirtualAddress();
    dispatchDesc.MissShaderTable.SizeInBytes = m_Impl->MissShaderTable.GetSizeInBytes();
    dispatchDesc.MissShaderTable.StrideInBytes = m_Impl->MissShaderTable.GetStrideInBytes();
    dispatchDesc.HitGroupTable.StartAddress = m_Impl->HitGroupShaderTable.GetGpuVirtualAddress();
    dispatchDesc.HitGroupTable.SizeInBytes = m_Impl->HitGroupShaderTable.GetSizeInBytes();
    dispatchDesc.HitGroupTable.StrideInBytes = m_Impl->HitGroupShaderTable.GetStrideInBytes();
    dispatchDesc.Width = width;
    dispatchDesc.Height = height;
    dispatchDesc.Depth = depth;

    commandList.DispatchRays(dispatchDesc);

    for (uint32_t bindingIndex = 0; bindingIndex < m_Impl->Desc.Bindings.size(); ++bindingIndex)
    {
        const RayTracingShaderBindingDesc& binding = m_Impl->Desc.Bindings[bindingIndex];
        if (binding.Type != RayTracingShaderBindingType::OutputTexture)
        {
            continue;
        }

        const auto boundBindingIt = m_Impl->BoundBindings.find(bindingIndex);
        Assert(boundBindingIt != m_Impl->BoundBindings.end() && boundBindingIt->second.TextureResource != nullptr, "Ray tracing output texture is not bound.");
        commandList.UavBarrier(*boundBindingIt->second.TextureResource);
    }
}
//Modify End
