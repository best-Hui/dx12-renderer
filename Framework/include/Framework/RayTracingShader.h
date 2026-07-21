#pragma once
//Modify Begin:2026-07-21 by BestHui

#include <Framework/RayTracingAccelerationStructure.h>

#include <d3d12.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

class CommandList;
class Resource;
class ShaderBlob;
class StructuredBuffer;
class Texture;

class RayTracingShader
{
public:
    explicit RayTracingShader(const ShaderBlob& shaderLibrary);
    ~RayTracingShader();

    RayTracingShader(const RayTracingShader&) = delete;
    RayTracingShader& operator=(const RayTracingShader&) = delete;
    RayTracingShader(RayTracingShader&&) noexcept;
    RayTracingShader& operator=(RayTracingShader&&) noexcept;

    static bool IsSupported();

    void SetOutputTexture(std::string_view name, const std::shared_ptr<Texture>& texture);
    void SetAccelerationStructure(std::string_view name, const RayTracingAccelerationStructure& accelerationStructure);
    void SetConstantBufferData(std::string_view name, const void* data, size_t size);
    void SetStructuredBuffer(std::string_view name, const StructuredBuffer& buffer);
    void SetTextureArray(std::string_view name, const std::vector<std::shared_ptr<Texture>>& textures);

    void Dispatch(CommandList& commandList, std::string_view rayGenerationShaderName, uint32_t width, uint32_t height, uint32_t depth = 1);

private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};
//Modify End
