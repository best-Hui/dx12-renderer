//Modify Begin:2026-07-21 by BestHui
#include <Framework/RayTracingShader.h>

#include <DX12Library/Application.h>
#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/RootSignature.h>
#include <DX12Library/StructuredBuffer.h>
#include <DX12Library/Texture.h>
#include <Framework/Mesh.h>
#include <Framework/ShaderBlob.h>

#include <d3dx12.h>

#include <cstring>

using Microsoft::WRL::ComPtr;

namespace
{
    constexpr uint32_t MaxDescriptorCount = 2048;
    constexpr uint32_t OutputDescriptorIndex = 0;
    constexpr LPCWSTR RayGenerationShaderName = L"RayGen";
    constexpr LPCWSTR MissShaderName = L"Miss";
    constexpr LPCWSTR ClosestHitShaderName = L"ClosestHit";
    constexpr LPCWSTR HitGroupName = L"HitGroup";

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

    ComPtr<ID3D12Device5> GetDxrDevice()
    {
        ComPtr<ID3D12Device5> device5;
        ThrowIfFailed(Application::Get().GetDevice().As(&device5));
        return device5;
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

    class ShaderTable
    {
    public:
        void Reset(
            const ComPtr<ID3D12Device>& device,
            const std::vector<const void*>& shaderIdentifiers,
            const uint32_t shaderIdentifierSize,
            const wchar_t* name)
        {
            m_StrideInBytes = Math::AlignUp(shaderIdentifierSize, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
            m_SizeInBytes = static_cast<uint64_t>(m_StrideInBytes) * shaderIdentifiers.size();

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

            for (size_t i = 0; i < shaderIdentifiers.size(); ++i)
            {
                std::memcpy(mappedData + i * m_StrideInBytes, shaderIdentifiers[i], shaderIdentifierSize);
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

    class RayTracingPipeline
    {
    public:
        explicit RayTracingPipeline(const D3D12_SHADER_BYTECODE& shaderLibrary)
        {
            CreateGlobalRootSignature();
            CreateStateObject(shaderLibrary);
        }

        RootSignature& GetGlobalRootSignature()
        {
            return *m_GlobalRootSignature;
        }

        const ComPtr<ID3D12StateObject>& GetStateObject() const
        {
            return m_StateObject;
        }

        const void* GetRayGenerationShaderIdentifier() const
        {
            return m_StateObjectProperties->GetShaderIdentifier(RayGenerationShaderName);
        }

        const void* GetMissShaderIdentifier() const
        {
            return m_StateObjectProperties->GetShaderIdentifier(MissShaderName);
        }

        const void* GetHitGroupShaderIdentifier() const
        {
            return m_StateObjectProperties->GetShaderIdentifier(HitGroupName);
        }

    private:
        void CreateGlobalRootSignature()
        {
            using RootParameter = CD3DX12_ROOT_PARAMETER1;
            using DescriptorRange = CD3DX12_DESCRIPTOR_RANGE1;
            using StaticSampler = CD3DX12_STATIC_SAMPLER_DESC;

            DescriptorRange outputRange;
            outputRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0);

            DescriptorRange vertexBufferRange;
            vertexBufferRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, MaxDescriptorCount, 0, 1);

            DescriptorRange indexBufferRange;
            indexBufferRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, MaxDescriptorCount, 0, 2);

            DescriptorRange textureRange;
            textureRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, MaxDescriptorCount, 0, 3);

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

        void CreateStateObject(const D3D12_SHADER_BYTECODE& shaderLibrary)
        {
            const auto device = GetDxrDevice();

            CD3DX12_STATE_OBJECT_DESC stateObjectDesc(D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);

            auto dxilLibrary = stateObjectDesc.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
            D3D12_SHADER_BYTECODE libraryBytecode = shaderLibrary;
            dxilLibrary->SetDXILLibrary(&libraryBytecode);
            dxilLibrary->DefineExport(RayGenerationShaderName);
            dxilLibrary->DefineExport(MissShaderName);
            dxilLibrary->DefineExport(ClosestHitShaderName);

            auto hitGroup = stateObjectDesc.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
            hitGroup->SetHitGroupExport(HitGroupName);
            hitGroup->SetClosestHitShaderImport(ClosestHitShaderName);
            hitGroup->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);

            auto shaderConfig = stateObjectDesc.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
            shaderConfig->Config(sizeof(float) * 8, sizeof(float) * 2);

            auto globalRootSignature = stateObjectDesc.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
            globalRootSignature->SetRootSignature(m_GlobalRootSignature->GetRootSignature().Get());

            auto pipelineConfig = stateObjectDesc.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
            pipelineConfig->Config(1);

            ThrowIfFailed(device->CreateStateObject(stateObjectDesc, IID_PPV_ARGS(&m_StateObject)));
            ThrowIfFailed(m_StateObject.As(&m_StateObjectProperties));
        }

        std::shared_ptr<RootSignature> m_GlobalRootSignature;
        ComPtr<ID3D12StateObject> m_StateObject;
        ComPtr<ID3D12StateObjectProperties> m_StateObjectProperties;
    };
}

struct RayTracingShader::Impl
{
    explicit Impl(const ShaderBlob& shaderLibrary)
        : Pipeline(
            D3D12_SHADER_BYTECODE{
                shaderLibrary.GetBlob()->GetBufferPointer(),
                shaderLibrary.GetBlob()->GetBufferSize()
            })
    {
        BuildShaderTables();
    }

    void BuildShaderTables()
    {
        const auto device = Application::Get().GetDevice();

        RayGenerationShaderTable.Reset(
            device,
            { Pipeline.GetRayGenerationShaderIdentifier() },
            D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES,
            L"Ray Generation Shader Table");
        MissShaderTable.Reset(
            device,
            { Pipeline.GetMissShaderIdentifier() },
            D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES,
            L"Miss Shader Table");
        HitGroupShaderTable.Reset(
            device,
            { Pipeline.GetHitGroupShaderIdentifier() },
            D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES,
            L"Hit Group Shader Table");
    }

    void RebuildDescriptorHeap()
    {
        Assert(OutputTexture != nullptr, "Ray tracing output texture is not bound.");
        Assert(AccelerationStructure != nullptr, "Ray tracing acceleration structure is not bound.");

        const auto device = Application::Get().GetDevice();
        const std::vector<std::shared_ptr<Mesh>>& meshes = AccelerationStructure->GetMeshes();
        const uint32_t meshCount = static_cast<uint32_t>(meshes.size());
        const uint32_t textureCount = static_cast<uint32_t>(Textures.size());

        Assert(meshCount <= MaxDescriptorCount, "Ray tracing scene uses more mesh descriptors than the root signature allows.");
        Assert(textureCount <= MaxDescriptorCount, "Ray tracing shader uses more texture descriptors than the root signature allows.");

        VertexBuffersDescriptorStart = 1;
        IndexBuffersDescriptorStart = VertexBuffersDescriptorStart + MaxDescriptorCount;
        TexturesDescriptorStart = IndexBuffersDescriptorStart + MaxDescriptorCount;

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.NumDescriptors = TexturesDescriptorStart + MaxDescriptorCount;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&DescriptorHeap)));

        DescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        const D3D12_SHADER_RESOURCE_VIEW_DESC nullVertexSrvDesc = CreateNullVertexBufferSrvDesc();
        const D3D12_SHADER_RESOURCE_VIEW_DESC nullIndexSrvDesc = CreateNullIndexBufferSrvDesc();
        const D3D12_SHADER_RESOURCE_VIEW_DESC nullTextureSrvDesc = CreateNullTextureSrvDesc();
        for (uint32_t i = 0; i < MaxDescriptorCount; ++i)
        {
            device->CreateShaderResourceView(nullptr, &nullVertexSrvDesc, GetCpuDescriptorHandle(VertexBuffersDescriptorStart + i));
            device->CreateShaderResourceView(nullptr, &nullIndexSrvDesc, GetCpuDescriptorHandle(IndexBuffersDescriptorStart + i));
            device->CreateShaderResourceView(nullptr, &nullTextureSrvDesc, GetCpuDescriptorHandle(TexturesDescriptorStart + i));
        }

        device->CopyDescriptorsSimple(
            1,
            GetCpuDescriptorHandle(OutputDescriptorIndex),
            OutputTexture->GetUnorderedAccessView(),
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        for (uint32_t i = 0; i < meshCount; ++i)
        {
            const auto& mesh = meshes[i];
            const VertexBuffer& vertexBuffer = mesh->GetVertexBuffer();
            const IndexBuffer& indexBuffer = mesh->GetIndexBuffer();

            const D3D12_SHADER_RESOURCE_VIEW_DESC vertexSrvDesc = CreateVertexBufferSrvDesc(vertexBuffer);
            device->CreateShaderResourceView(
                vertexBuffer.GetD3D12Resource().Get(),
                &vertexSrvDesc,
                GetCpuDescriptorHandle(VertexBuffersDescriptorStart + i));

            const D3D12_SHADER_RESOURCE_VIEW_DESC indexSrvDesc = CreateIndexBufferSrvDesc(indexBuffer);
            device->CreateShaderResourceView(
                indexBuffer.GetD3D12Resource().Get(),
                &indexSrvDesc,
                GetCpuDescriptorHandle(IndexBuffersDescriptorStart + i));
        }

        for (uint32_t i = 0; i < textureCount; ++i)
        {
            device->CopyDescriptorsSimple(
                1,
                GetCpuDescriptorHandle(TexturesDescriptorStart + i),
                Textures[i]->GetShaderResourceView(),
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetGpuDescriptorHandle(const uint32_t descriptorIndex) const
    {
        auto handle = DescriptorHeap->GetGPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(descriptorIndex) * DescriptorSize;
        return handle;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GetCpuDescriptorHandle(const uint32_t descriptorIndex) const
    {
        auto handle = DescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(descriptorIndex) * DescriptorSize;
        return handle;
    }

    RayTracingPipeline Pipeline;
    ShaderTable RayGenerationShaderTable;
    ShaderTable MissShaderTable;
    ShaderTable HitGroupShaderTable;

    std::shared_ptr<Texture> OutputTexture;
    const RayTracingAccelerationStructure* AccelerationStructure = nullptr;
    const StructuredBuffer* MaterialBuffer = nullptr;
    const StructuredBuffer* GeometryBuffer = nullptr;
    std::vector<uint8_t> ConstantBufferData;
    std::vector<std::shared_ptr<Texture>> Textures;

    ComPtr<ID3D12DescriptorHeap> DescriptorHeap;
    uint32_t DescriptorSize = 0;
    uint32_t VertexBuffersDescriptorStart = 0;
    uint32_t IndexBuffersDescriptorStart = 0;
    uint32_t TexturesDescriptorStart = 0;
    bool DescriptorsDirty = true;
};

RayTracingShader::RayTracingShader(const ShaderBlob& shaderLibrary)
    : m_Impl(std::make_unique<Impl>(shaderLibrary))
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

void RayTracingShader::SetOutputTexture(std::string_view name, const std::shared_ptr<Texture>& texture)
{
    Assert(name == "Output", "Unknown ray tracing output texture binding.");
    Assert(texture != nullptr, "Ray tracing output texture must not be null.");
    m_Impl->OutputTexture = texture;
    m_Impl->DescriptorsDirty = true;
}

void RayTracingShader::SetAccelerationStructure(std::string_view name, const RayTracingAccelerationStructure& accelerationStructure)
{
    Assert(name == "Scene", "Unknown ray tracing acceleration structure binding.");
    m_Impl->AccelerationStructure = &accelerationStructure;
    m_Impl->DescriptorsDirty = true;
}

void RayTracingShader::SetConstantBufferData(std::string_view name, const void* data, const size_t size)
{
    Assert(name == "CameraConstants", "Unknown ray tracing constant buffer binding.");
    Assert(data != nullptr && size > 0, "Ray tracing constant buffer data must not be empty.");
    m_Impl->ConstantBufferData.resize(size);
    std::memcpy(m_Impl->ConstantBufferData.data(), data, size);
}

void RayTracingShader::SetStructuredBuffer(std::string_view name, const StructuredBuffer& buffer)
{
    if (name == "Materials")
    {
        m_Impl->MaterialBuffer = &buffer;
        return;
    }

    if (name == "Geometries")
    {
        m_Impl->GeometryBuffer = &buffer;
        return;
    }

    Assert(false, "Unknown ray tracing structured buffer binding.");
}

void RayTracingShader::SetTextureArray(std::string_view name, const std::vector<std::shared_ptr<Texture>>& textures)
{
    Assert(name == "Textures", "Unknown ray tracing texture array binding.");
    Assert(!textures.empty(), "Ray tracing texture array must not be empty.");
    m_Impl->Textures = textures;
    m_Impl->DescriptorsDirty = true;
}

void RayTracingShader::Dispatch(
    CommandList& commandList,
    std::string_view rayGenerationShaderName,
    const uint32_t width,
    const uint32_t height,
    const uint32_t depth)
{
    Assert(rayGenerationShaderName == "RayGen", "This ray tracing shader wrapper currently supports the RayGen export.");
    Assert(m_Impl->OutputTexture != nullptr, "Ray tracing output texture is not bound.");
    Assert(m_Impl->AccelerationStructure != nullptr, "Ray tracing acceleration structure is not bound.");
    Assert(m_Impl->MaterialBuffer != nullptr, "Ray tracing material buffer is not bound.");
    Assert(m_Impl->GeometryBuffer != nullptr, "Ray tracing geometry buffer is not bound.");
    Assert(!m_Impl->ConstantBufferData.empty(), "Ray tracing constant buffer is not bound.");

    if (m_Impl->DescriptorsDirty)
    {
        m_Impl->RebuildDescriptorHeap();
        m_Impl->DescriptorsDirty = false;
    }

    commandList.TransitionBarrier(*m_Impl->OutputTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandList.TransitionBarrier(*m_Impl->MaterialBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    commandList.TransitionBarrier(*m_Impl->GeometryBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    for (const auto& texture : m_Impl->Textures)
    {
        commandList.TransitionBarrier(*texture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    const auto constantBufferAllocation = commandList.AllocateInUploadBuffer(
        m_Impl->ConstantBufferData.size(),
        D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
    std::memcpy(constantBufferAllocation.Cpu, m_Impl->ConstantBufferData.data(), m_Impl->ConstantBufferData.size());

    const auto d3d12CommandList = commandList.GetGraphicsCommandList();

    commandList.SetRaytracingPipelineState(m_Impl->Pipeline.GetStateObject());
    d3d12CommandList->SetComputeRootSignature(m_Impl->Pipeline.GetGlobalRootSignature().GetRootSignature().Get());

    ID3D12DescriptorHeap* descriptorHeaps[] = { m_Impl->DescriptorHeap.Get() };
    d3d12CommandList->SetDescriptorHeaps(1, descriptorHeaps);

    d3d12CommandList->SetComputeRootDescriptorTable(Output, m_Impl->GetGpuDescriptorHandle(OutputDescriptorIndex));
    d3d12CommandList->SetComputeRootShaderResourceView(AccelerationStructure, m_Impl->AccelerationStructure->GetGpuVirtualAddress());
    d3d12CommandList->SetComputeRootConstantBufferView(Camera, constantBufferAllocation.Gpu);
    d3d12CommandList->SetComputeRootShaderResourceView(Materials, m_Impl->MaterialBuffer->GetD3D12Resource()->GetGPUVirtualAddress());
    d3d12CommandList->SetComputeRootShaderResourceView(Geometries, m_Impl->GeometryBuffer->GetD3D12Resource()->GetGPUVirtualAddress());
    d3d12CommandList->SetComputeRootDescriptorTable(VertexBuffers, m_Impl->GetGpuDescriptorHandle(m_Impl->VertexBuffersDescriptorStart));
    d3d12CommandList->SetComputeRootDescriptorTable(IndexBuffers, m_Impl->GetGpuDescriptorHandle(m_Impl->IndexBuffersDescriptorStart));
    d3d12CommandList->SetComputeRootDescriptorTable(Textures, m_Impl->GetGpuDescriptorHandle(m_Impl->TexturesDescriptorStart));

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
    commandList.UavBarrier(*m_Impl->OutputTexture);
}
//Modify End
