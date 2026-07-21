#pragma once

#include <DirectXMath.h>

#include <memory>

class Mesh;
class Texture;

namespace Dxr
{
    struct MaterialData
    {
        DirectX::XMFLOAT4 Diffuse = { 1.0f, 1.0f, 1.0f, 1.0f };
        DirectX::XMFLOAT4 TilingOffset = { 1.0f, 1.0f, 0.0f, 0.0f };
        uint32_t DiffuseTextureIndex = 0;
        uint32_t Padding0 = 0;
        uint32_t Padding1 = 0;
        uint32_t Padding2 = 0;
    };

    struct GeometryData
    {
        uint32_t VertexBufferIndex = 0;
        uint32_t IndexBufferIndex = 0;
        uint32_t MaterialIndex = 0;
        uint32_t Padding = 0;
    };

    struct MeshInstance
    {
        std::shared_ptr<Mesh> Mesh;
        DirectX::XMMATRIX Transform = DirectX::XMMatrixIdentity();
        uint32_t MaterialIndex = 0;
    };
}
