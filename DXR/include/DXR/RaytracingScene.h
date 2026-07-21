#pragma once

#include <DXR/RaytracingTypes.h>

#include <d3d12.h>
#include <wrl.h>

#include <map>
#include <memory>
#include <vector>

class CommandList;
class Mesh;

namespace Dxr
{
    class RaytracingScene
    {
    public:
        void Build(CommandList& commandList, const std::vector<MeshInstance>& instances);

        D3D12_GPU_VIRTUAL_ADDRESS GetTopLevelAccelerationStructureGpuAddress() const;
        const std::vector<std::shared_ptr<Mesh>>& GetMeshes() const;
        const std::vector<GeometryData>& GetGeometryData() const;
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
            const std::vector<MeshInstance>& instances,
            const std::map<const Mesh*, uint32_t>& meshToBlasIndex);

        std::vector<BottomLevelAccelerationStructure> m_BottomLevelAccelerationStructures;
        Microsoft::WRL::ComPtr<ID3D12Resource> m_TopLevelAccelerationStructure;
        Microsoft::WRL::ComPtr<ID3D12Resource> m_InstanceDescUpload;
        std::vector<std::shared_ptr<Mesh>> m_Meshes;
        std::vector<GeometryData> m_GeometryData;
    };
}
