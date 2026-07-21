#pragma once
//Modify Begin:2026-07-21 by BestHui

#include <DirectXMath.h>

#include <d3d12.h>
#include <wrl.h>

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

class CommandList;
class Mesh;

struct RayTracingGeometryData
{
    uint32_t VertexBufferIndex = 0;
    uint32_t IndexBufferIndex = 0;
    uint32_t MaterialIndex = 0;
    uint32_t Padding = 0;
};

struct RayTracingMeshInstance
{
    std::shared_ptr<Mesh> Mesh;
    DirectX::XMMATRIX Transform = DirectX::XMMatrixIdentity();
    uint32_t MaterialIndex = 0;
};

class RayTracingAccelerationStructure
{
public:
    void Build(CommandList& commandList, const std::vector<RayTracingMeshInstance>& instances);

    D3D12_GPU_VIRTUAL_ADDRESS GetGpuVirtualAddress() const;
    const std::vector<std::shared_ptr<Mesh>>& GetMeshes() const;
    const std::vector<RayTracingGeometryData>& GetGeometryData() const;
    uint32_t GetInstanceCount() const;

private:
    struct BottomLevelAccelerationStructure
    {
        std::shared_ptr<Mesh> Mesh;
        Microsoft::WRL::ComPtr<ID3D12Resource> Resource;
    };

    Microsoft::WRL::ComPtr<ID3D12Resource> CreateAccelerationStructureBuffer(
        uint64_t size,
        D3D12_RESOURCE_STATES initialState,
        const wchar_t* name) const;

    Microsoft::WRL::ComPtr<ID3D12Resource> CreateUploadBuffer(
        const void* data,
        uint64_t size,
        const wchar_t* name) const;

    BottomLevelAccelerationStructure BuildBottomLevelAccelerationStructure(
        CommandList& commandList,
        const std::shared_ptr<Mesh>& mesh) const;

    void BuildTopLevelAccelerationStructure(
        CommandList& commandList,
        const std::vector<RayTracingMeshInstance>& instances,
        const std::map<const Mesh*, uint32_t>& meshToBlasIndex);

    std::vector<BottomLevelAccelerationStructure> m_BottomLevelAccelerationStructures;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_TopLevelAccelerationStructure;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_InstanceDescUpload;
    std::vector<std::shared_ptr<Mesh>> m_Meshes;
    std::vector<RayTracingGeometryData> m_GeometryData;
};
//Modify End
