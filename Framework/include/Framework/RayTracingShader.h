#pragma once
//Modify Begin:2026-07-21 by BestHui

#include <Framework/RayTracingAccelerationStructure.h>

#include <d3d12.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class CommandList;
class ShaderBlob;
class StructuredBuffer;
class Texture;

enum class RayTracingShaderBindingType
{
    OutputTexture,
    AccelerationStructure,
    ConstantBuffer,
    StructuredBuffer,
    TextureArray,
    VertexBufferArray,
    IndexBufferArray
};

struct RayTracingShaderBindingDesc
{
    std::string Name;
    RayTracingShaderBindingType Type = RayTracingShaderBindingType::StructuredBuffer;
    uint32_t ShaderRegister = 0;
    uint32_t RegisterSpace = 0;
    uint32_t DescriptorCount = 1;
};

struct RayTracingHitGroupDesc
{
    std::wstring Name;
    std::wstring ClosestHitShader;
    std::wstring AnyHitShader;
    std::wstring IntersectionShader;
    D3D12_HIT_GROUP_TYPE Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
};

struct RayTracingShaderRecordDesc
{
    std::wstring ExportName;
    std::vector<uint8_t> LocalRootArguments;
};

struct RayTracingShaderPassDesc
{
    std::string Name;
    std::wstring RayGenerationShader;
    std::vector<RayTracingShaderRecordDesc> MissShaderRecords;
    std::vector<RayTracingShaderRecordDesc> HitGroupRecords;
};

struct RayTracingPipelineDesc
{
    std::vector<std::wstring> Exports;
    std::vector<RayTracingHitGroupDesc> HitGroups;
    std::vector<RayTracingShaderBindingDesc> Bindings;
    std::vector<RayTracingShaderPassDesc> Passes;
    uint32_t PayloadSizeInBytes = sizeof(float) * 8;
    uint32_t AttributeSizeInBytes = sizeof(float) * 2;
    uint32_t MaxTraceRecursionDepth = 1;
    uint32_t MaxDescriptorCount = 2048;
};

class RayTracingShader
{
public:
    explicit RayTracingShader(const ShaderBlob& shaderLibrary);
    RayTracingShader(const ShaderBlob& shaderLibrary, RayTracingPipelineDesc desc);
    ~RayTracingShader();

    RayTracingShader(const RayTracingShader&) = delete;
    RayTracingShader& operator=(const RayTracingShader&) = delete;
    RayTracingShader(RayTracingShader&&) noexcept;
    RayTracingShader& operator=(RayTracingShader&&) noexcept;

    static bool IsSupported();
    static RayTracingPipelineDesc CreateDefaultPipelineDesc();

    const RayTracingPipelineDesc& GetDesc() const;

    void SetOutputTexture(std::string_view name, const std::shared_ptr<Texture>& texture);
    void SetAccelerationStructure(std::string_view name, const RayTracingAccelerationStructure& accelerationStructure);
    void SetConstantBufferData(std::string_view name, const void* data, size_t size);
    void SetStructuredBuffer(std::string_view name, const StructuredBuffer& buffer);
    void SetTextureArray(std::string_view name, const std::vector<std::shared_ptr<Texture>>& textures);
    void SetTextureArray(
        std::string_view name,
        const std::vector<std::shared_ptr<Texture>>& textures,
        const std::vector<D3D12_SHADER_RESOURCE_VIEW_DESC>& srvDescs);

    void Dispatch(CommandList& commandList, std::string_view passName, uint32_t width, uint32_t height, uint32_t depth = 1);

private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};
//Modify End
