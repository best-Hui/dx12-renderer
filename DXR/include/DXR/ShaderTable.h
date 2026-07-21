#pragma once

#include <d3d12.h>
#include <wrl.h>

#include <cstdint>
#include <vector>

namespace Dxr
{
    class ShaderTable
    {
    public:
        void Reset(
            const Microsoft::WRL::ComPtr<ID3D12Device>& device,
            const std::vector<const void*>& shaderIdentifiers,
            uint32_t shaderIdentifierSize,
            const wchar_t* name);

        D3D12_GPU_VIRTUAL_ADDRESS GetGpuVirtualAddress() const;
        uint64_t GetSizeInBytes() const;
        uint32_t GetStrideInBytes() const;
        ID3D12Resource* GetResource() const;

    private:
        Microsoft::WRL::ComPtr<ID3D12Resource> m_Resource;
        uint64_t m_SizeInBytes = 0;
        uint32_t m_StrideInBytes = 0;
    };
}
