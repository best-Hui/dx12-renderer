#include <DXR/ShaderTable.h>

#include <DX12Library/Helpers.h>
#include <d3dx12.h>

#include <cstring>

using Microsoft::WRL::ComPtr;

namespace Dxr
{
    void ShaderTable::Reset(
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

    D3D12_GPU_VIRTUAL_ADDRESS ShaderTable::GetGpuVirtualAddress() const
    {
        return m_Resource->GetGPUVirtualAddress();
    }

    uint64_t ShaderTable::GetSizeInBytes() const
    {
        return m_SizeInBytes;
    }

    uint32_t ShaderTable::GetStrideInBytes() const
    {
        return m_StrideInBytes;
    }

    ID3D12Resource* ShaderTable::GetResource() const
    {
        return m_Resource.Get();
    }
}
