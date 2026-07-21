//Modify Begin:2026-07-21 by BestHui
#include <Framework/RayTracingAccelerationStructure.h>

#include <DX12Library/Application.h>
#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>
#include <Framework/Mesh.h>

#include <d3dx12.h>

#include <cstring>

using Microsoft::WRL::ComPtr;

namespace
{
    ComPtr<ID3D12Device5> GetDxrDevice()
    {
        ComPtr<ID3D12Device5> device5;
        ThrowIfFailed(Application::Get().GetDevice().As(&device5));
        return device5;
    }
}

void RayTracingAccelerationStructure::Build(CommandList& commandList, const std::vector<RayTracingMeshInstance>& instances)
{
    m_BottomLevelAccelerationStructures.clear();
    m_TopLevelAccelerationStructure.Reset();
    m_InstanceDescUpload.Reset();
    m_Meshes.clear();
    m_GeometryData.clear();

    std::map<const Mesh*, uint32_t> meshToBlasIndex;

    for (const RayTracingMeshInstance& instance : instances)
    {
        if (meshToBlasIndex.contains(instance.Mesh.get()))
        {
            continue;
        }

        const uint32_t meshIndex = static_cast<uint32_t>(m_BottomLevelAccelerationStructures.size());
        meshToBlasIndex.emplace(instance.Mesh.get(), meshIndex);
        m_BottomLevelAccelerationStructures.push_back(BuildBottomLevelAccelerationStructure(commandList, instance.Mesh));
        m_Meshes.push_back(instance.Mesh);
    }

    BuildTopLevelAccelerationStructure(commandList, instances, meshToBlasIndex);
}

D3D12_GPU_VIRTUAL_ADDRESS RayTracingAccelerationStructure::GetGpuVirtualAddress() const
{
    return m_TopLevelAccelerationStructure->GetGPUVirtualAddress();
}

const std::vector<std::shared_ptr<Mesh>>& RayTracingAccelerationStructure::GetMeshes() const
{
    return m_Meshes;
}

const std::vector<RayTracingGeometryData>& RayTracingAccelerationStructure::GetGeometryData() const
{
    return m_GeometryData;
}

uint32_t RayTracingAccelerationStructure::GetInstanceCount() const
{
    return static_cast<uint32_t>(m_GeometryData.size());
}

ComPtr<ID3D12Resource> RayTracingAccelerationStructure::CreateAccelerationStructureBuffer(
    const uint64_t size,
    const D3D12_RESOURCE_STATES initialState,
    const wchar_t* name) const
{
    const auto device = Application::Get().GetDevice();
    const auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    const auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(size, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    ComPtr<ID3D12Resource> resource;
    ThrowIfFailed(device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        initialState,
        nullptr,
        IID_PPV_ARGS(&resource)));

    if (name != nullptr)
    {
        resource->SetName(name);
    }

    return resource;
}

ComPtr<ID3D12Resource> RayTracingAccelerationStructure::CreateUploadBuffer(
    const void* data,
    const uint64_t size,
    const wchar_t* name) const
{
    const auto device = Application::Get().GetDevice();
    const auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    const auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(size);

    ComPtr<ID3D12Resource> resource;
    ThrowIfFailed(device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&resource)));

    if (name != nullptr)
    {
        resource->SetName(name);
    }

    void* mappedData = nullptr;
    ThrowIfFailed(resource->Map(0, nullptr, &mappedData));
    std::memcpy(mappedData, data, size);
    resource->Unmap(0, nullptr);

    return resource;
}

RayTracingAccelerationStructure::BottomLevelAccelerationStructure RayTracingAccelerationStructure::BuildBottomLevelAccelerationStructure(
    CommandList& commandList,
    const std::shared_ptr<Mesh>& mesh) const
{
        const auto device = GetDxrDevice();
        const VertexBuffer& vertexBuffer = mesh->GetVertexBuffer();
        const IndexBuffer& indexBuffer = mesh->GetIndexBuffer();

        commandList.TransitionBarrier(vertexBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        commandList.TransitionBarrier(indexBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc = {};
        geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
        geometryDesc.Triangles.VertexBuffer.StartAddress = vertexBuffer.GetD3D12Resource()->GetGPUVirtualAddress();
        geometryDesc.Triangles.VertexBuffer.StrideInBytes = vertexBuffer.GetVertexStride();
        geometryDesc.Triangles.VertexCount = static_cast<UINT>(vertexBuffer.GetNumVertices());
        geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
        geometryDesc.Triangles.IndexBuffer = indexBuffer.GetD3D12Resource()->GetGPUVirtualAddress();
        geometryDesc.Triangles.IndexCount = static_cast<UINT>(indexBuffer.GetNumIndices());
        geometryDesc.Triangles.IndexFormat = indexBuffer.GetIndexFormat();

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
        inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        inputs.NumDescs = 1;
        inputs.pGeometryDescs = &geometryDesc;

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo = {};
        device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuildInfo);
        Assert(prebuildInfo.ResultDataMaxSizeInBytes > 0, "Invalid BLAS prebuild info.");

        auto result = CreateAccelerationStructureBuffer(
            prebuildInfo.ResultDataMaxSizeInBytes,
            D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
            L"Ray Tracing Bottom Level Acceleration Structure");
        auto scratch = CreateAccelerationStructureBuffer(
            prebuildInfo.ScratchDataSizeInBytes,
            D3D12_RESOURCE_STATE_COMMON,
            L"Ray Tracing BLAS Scratch");

        const D3D12_RESOURCE_BARRIER scratchBarrier =
            CD3DX12_RESOURCE_BARRIER::Transition(scratch.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        commandList.GetGraphicsCommandList()->ResourceBarrier(1, &scratchBarrier);

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
        buildDesc.Inputs = inputs;
        buildDesc.ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress();
        buildDesc.DestAccelerationStructureData = result->GetGPUVirtualAddress();

        commandList.BuildRaytracingAccelerationStructure(buildDesc);
        const auto uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(result.Get());
        commandList.GetGraphicsCommandList()->ResourceBarrier(1, &uavBarrier);
        commandList.TrackObject(scratch);

        return { mesh, result };
    }

    void RayTracingAccelerationStructure::BuildTopLevelAccelerationStructure(
        CommandList& commandList,
        const std::vector<RayTracingMeshInstance>& instances,
        const std::map<const Mesh*, uint32_t>& meshToBlasIndex)
    {
        const auto device = GetDxrDevice();

        std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs;
        instanceDescs.reserve(instances.size());
        m_GeometryData.reserve(instances.size());

        for (const RayTracingMeshInstance& instance : instances)
        {
            const uint32_t blasIndex = meshToBlasIndex.at(instance.Mesh.get());

            D3D12_RAYTRACING_INSTANCE_DESC instanceDesc = {};
            DirectX::XMStoreFloat3x4(reinterpret_cast<DirectX::XMFLOAT3X4*>(instanceDesc.Transform), instance.Transform);
            instanceDesc.InstanceID = static_cast<UINT>(m_GeometryData.size());
            instanceDesc.InstanceMask = 0xFF;
            instanceDesc.InstanceContributionToHitGroupIndex = 0;
            instanceDesc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
            instanceDesc.AccelerationStructure = m_BottomLevelAccelerationStructures[blasIndex].Resource->GetGPUVirtualAddress();
            instanceDescs.push_back(instanceDesc);

            m_GeometryData.push_back({
                blasIndex,
                blasIndex,
                instance.MaterialIndex,
                0
            });
        }

        m_InstanceDescUpload = CreateUploadBuffer(
            instanceDescs.data(),
            sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * instanceDescs.size(),
            L"Ray Tracing Instance Descriptions");

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
        inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
        inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        inputs.NumDescs = static_cast<UINT>(instanceDescs.size());
        inputs.InstanceDescs = m_InstanceDescUpload->GetGPUVirtualAddress();

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo = {};
        device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuildInfo);
        Assert(prebuildInfo.ResultDataMaxSizeInBytes > 0, "Invalid TLAS prebuild info.");

        m_TopLevelAccelerationStructure = CreateAccelerationStructureBuffer(
            prebuildInfo.ResultDataMaxSizeInBytes,
            D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
            L"Ray Tracing Top Level Acceleration Structure");
        auto scratch = CreateAccelerationStructureBuffer(
            prebuildInfo.ScratchDataSizeInBytes,
            D3D12_RESOURCE_STATE_COMMON,
            L"Ray Tracing TLAS Scratch");

        const D3D12_RESOURCE_BARRIER scratchBarrier =
            CD3DX12_RESOURCE_BARRIER::Transition(scratch.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        commandList.GetGraphicsCommandList()->ResourceBarrier(1, &scratchBarrier);

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
        buildDesc.Inputs = inputs;
        buildDesc.ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress();
        buildDesc.DestAccelerationStructureData = m_TopLevelAccelerationStructure->GetGPUVirtualAddress();

        commandList.BuildRaytracingAccelerationStructure(buildDesc);
        const auto uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(m_TopLevelAccelerationStructure.Get());
        commandList.GetGraphicsCommandList()->ResourceBarrier(1, &uavBarrier);
        commandList.TrackObject(scratch);
        commandList.TrackObject(m_InstanceDescUpload);
}
//Modify End
