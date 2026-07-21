#include <DXR/RaytracingRenderer.h>

#include <DX12Library/Application.h>
#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>
#include <Framework/Mesh.h>

#include <d3dx12.h>

#include <cstring>

using Microsoft::WRL::ComPtr;

namespace
{
    constexpr uint32_t OutputDescriptorIndex = 0;

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
        desc.Format = DXGI_FORMAT_R32_TYPELESS;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.Buffer.NumElements = Math::AlignUp(indexBuffer.GetIndexBufferView().SizeInBytes, 4u) / 4u;
        desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
        return desc;
    }
}

namespace Dxr
{
    RaytracingRenderer::RaytracingRenderer(const ShaderBlob& shaderLibrary)
        : m_Pipeline(shaderLibrary)
        , m_MaterialBuffer(L"DXR Materials")
        , m_GeometryBuffer(L"DXR Geometry Data")
    {
        BuildShaderTables();
    }

    void RaytracingRenderer::Resize(const uint32_t width, const uint32_t height)
    {
        if (m_OutputTexture != nullptr && m_Width == width && m_Height == height)
        {
            return;
        }

        m_Width = std::max(1u, width);
        m_Height = std::max(1u, height);

        const auto outputDesc = CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_R8G8B8A8_UNORM,
            m_Width,
            m_Height,
            1,
            1,
            1,
            0,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

        m_OutputTexture = std::make_shared<Texture>(outputDesc, nullptr, TextureUsageType::Other, L"DXR Output");
        m_DescriptorsDirty = true;
    }

    void RaytracingRenderer::SetMaterials(
        CommandList& commandList,
        const std::vector<MaterialData>& materials,
        const std::vector<std::shared_ptr<Texture>>& textures)
    {
        Assert(!materials.empty(), "DXR material list must not be empty.");
        Assert(!textures.empty(), "DXR texture list must not be empty.");

        m_Textures = textures;
        commandList.CopyStructuredBuffer(m_MaterialBuffer, materials);
        m_DescriptorsDirty = true;
    }

    void RaytracingRenderer::SetScene(CommandList& commandList, const RaytracingScene& scene)
    {
        const std::vector<GeometryData>& geometryData = scene.GetGeometryData();
        Assert(!geometryData.empty(), "DXR scene must contain geometry.");
        commandList.CopyStructuredBuffer(m_GeometryBuffer, geometryData);
        m_DescriptorsDirty = true;
    }

    void RaytracingRenderer::Render(CommandList& commandList, const RaytracingScene& scene, const CameraConstants& camera)
    {
        if (m_DescriptorsDirty)
        {
            RebuildDescriptorHeap(scene);
            m_DescriptorsDirty = false;
        }

        commandList.TransitionBarrier(*m_OutputTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        commandList.TransitionBarrier(m_MaterialBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        commandList.TransitionBarrier(m_GeometryBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        for (const auto& texture : m_Textures)
        {
            commandList.TransitionBarrier(*texture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }

        const auto cameraAllocation = commandList.AllocateInUploadBuffer(sizeof(CameraConstants), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
        std::memcpy(cameraAllocation.Cpu, &camera, sizeof(CameraConstants));

        commandList.SetRaytracingPipelineState(m_Pipeline.GetStateObject());
        commandList.SetComputeRootSignature(m_Pipeline.GetGlobalRootSignature());
        commandList.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_DescriptorHeap.Get());

        commandList.SetComputeRootDescriptorTable(RaytracingPipeline::RootParameters::Output, GetGpuDescriptorHandle(OutputDescriptorIndex));
        commandList.SetComputeRootShaderResourceView(RaytracingPipeline::RootParameters::AccelerationStructure, scene.GetTopLevelAccelerationStructureGpuAddress());
        commandList.SetComputeRootConstantBufferView(RaytracingPipeline::RootParameters::Camera, cameraAllocation.Gpu);
        commandList.SetComputeRootShaderResourceView(RaytracingPipeline::RootParameters::Materials, m_MaterialBuffer.GetD3D12Resource()->GetGPUVirtualAddress());
        commandList.SetComputeRootShaderResourceView(RaytracingPipeline::RootParameters::Geometries, m_GeometryBuffer.GetD3D12Resource()->GetGPUVirtualAddress());
        commandList.SetComputeRootDescriptorTable(RaytracingPipeline::RootParameters::VertexBuffers, GetGpuDescriptorHandle(m_VertexBuffersDescriptorStart));
        commandList.SetComputeRootDescriptorTable(RaytracingPipeline::RootParameters::IndexBuffers, GetGpuDescriptorHandle(m_IndexBuffersDescriptorStart));
        commandList.SetComputeRootDescriptorTable(RaytracingPipeline::RootParameters::Textures, GetGpuDescriptorHandle(m_TexturesDescriptorStart));

        D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
        dispatchDesc.RayGenerationShaderRecord.StartAddress = m_RayGenerationShaderTable.GetGpuVirtualAddress();
        dispatchDesc.RayGenerationShaderRecord.SizeInBytes = m_RayGenerationShaderTable.GetSizeInBytes();
        dispatchDesc.MissShaderTable.StartAddress = m_MissShaderTable.GetGpuVirtualAddress();
        dispatchDesc.MissShaderTable.SizeInBytes = m_MissShaderTable.GetSizeInBytes();
        dispatchDesc.MissShaderTable.StrideInBytes = m_MissShaderTable.GetStrideInBytes();
        dispatchDesc.HitGroupTable.StartAddress = m_HitGroupShaderTable.GetGpuVirtualAddress();
        dispatchDesc.HitGroupTable.SizeInBytes = m_HitGroupShaderTable.GetSizeInBytes();
        dispatchDesc.HitGroupTable.StrideInBytes = m_HitGroupShaderTable.GetStrideInBytes();
        dispatchDesc.Width = m_Width;
        dispatchDesc.Height = m_Height;
        dispatchDesc.Depth = 1;

        commandList.DispatchRays(dispatchDesc);
        commandList.UavBarrier(*m_OutputTexture);
    }

    const std::shared_ptr<Texture>& RaytracingRenderer::GetOutputTexture() const
    {
        return m_OutputTexture;
    }

    void RaytracingRenderer::BuildShaderTables()
    {
        const auto device = Application::Get().GetDevice();

        m_RayGenerationShaderTable.Reset(
            device,
            { m_Pipeline.GetRayGenerationShaderIdentifier() },
            D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES,
            L"DXR Ray Generation Shader Table");
        m_MissShaderTable.Reset(
            device,
            { m_Pipeline.GetMissShaderIdentifier() },
            D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES,
            L"DXR Miss Shader Table");
        m_HitGroupShaderTable.Reset(
            device,
            { m_Pipeline.GetHitGroupShaderIdentifier() },
            D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES,
            L"DXR Hit Group Shader Table");
    }

    void RaytracingRenderer::RebuildDescriptorHeap(const RaytracingScene& scene)
    {
        const auto device = Application::Get().GetDevice();
        const std::vector<std::shared_ptr<Mesh>>& meshes = scene.GetMeshes();
        const uint32_t meshCount = static_cast<uint32_t>(meshes.size());
        const uint32_t textureCount = static_cast<uint32_t>(m_Textures.size());

        m_VertexBuffersDescriptorStart = 1;
        m_IndexBuffersDescriptorStart = m_VertexBuffersDescriptorStart + meshCount;
        m_TexturesDescriptorStart = m_IndexBuffersDescriptorStart + meshCount;

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.NumDescriptors = m_TexturesDescriptorStart + textureCount;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_DescriptorHeap)));

        m_DescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        device->CopyDescriptorsSimple(
            1,
            GetCpuDescriptorHandle(OutputDescriptorIndex),
            m_OutputTexture->GetUnorderedAccessView(),
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
                GetCpuDescriptorHandle(m_VertexBuffersDescriptorStart + i));

            const D3D12_SHADER_RESOURCE_VIEW_DESC indexSrvDesc = CreateIndexBufferSrvDesc(indexBuffer);
            device->CreateShaderResourceView(
                indexBuffer.GetD3D12Resource().Get(),
                &indexSrvDesc,
                GetCpuDescriptorHandle(m_IndexBuffersDescriptorStart + i));
        }

        for (uint32_t i = 0; i < textureCount; ++i)
        {
            device->CopyDescriptorsSimple(
                1,
                GetCpuDescriptorHandle(m_TexturesDescriptorStart + i),
                m_Textures[i]->GetShaderResourceView(),
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
    }

    D3D12_GPU_DESCRIPTOR_HANDLE RaytracingRenderer::GetGpuDescriptorHandle(const uint32_t descriptorIndex) const
    {
        auto handle = m_DescriptorHeap->GetGPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(descriptorIndex) * m_DescriptorSize;
        return handle;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE RaytracingRenderer::GetCpuDescriptorHandle(const uint32_t descriptorIndex) const
    {
        auto handle = m_DescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(descriptorIndex) * m_DescriptorSize;
        return handle;
    }
}
