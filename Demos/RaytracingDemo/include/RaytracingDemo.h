#pragma once

#include <DX12Library/Camera.h>
#include <DX12Library/Game.h>

#include <DXR/RaytracingRenderer.h>
#include <DXR/RaytracingScene.h>
#include <DXR/RaytracingTypes.h>

#include <Framework/Light.h>
#include <Framework/Model.h>

#include <memory>
#include <string>
#include <vector>

struct GraphicsSettings;
class CommandList;

class RaytracingDemo final : public Game
{
public:
    using Base = Game;

    RaytracingDemo(const std::wstring& name, int width, int height, GraphicsSettings graphicsSettings);

    bool LoadContent() override;
    void UnloadContent() override;

protected:
    void OnUpdate(UpdateEventArgs& e) override;
    void OnRender(RenderEventArgs& e) override;
    void OnKeyPressed(KeyEventArgs& e) override;
    void OnKeyReleased(KeyEventArgs& e) override;
    void OnMouseMoved(MouseMotionEventArgs& e) override;
    void OnMouseWheel(MouseWheelEventArgs& e) override;
    void OnResize(ResizeEventArgs& e) override;

private:
    struct SceneObject
    {
        DirectX::XMMATRIX WorldMatrix = DirectX::XMMatrixIdentity();
        std::shared_ptr<Model> Model;
        uint32_t MaterialIndex = 0;
    };

    uint32_t AddTexture(CommandList& commandList, const std::wstring& path, TextureUsageType usage = TextureUsageType::Albedo);
    uint32_t AddMaterial(const Dxr::MaterialData& material);
    uint32_t AddDiffuseMaterial(
        const DirectX::XMFLOAT4& diffuse,
        const DirectX::XMFLOAT4& tilingOffset,
        uint32_t diffuseTextureIndex);
    void LoadDeferredLightingScene(CommandList& commandList);
    std::vector<Dxr::MeshInstance> CreateRaytracingInstances() const;

    Camera m_Camera;
    std::unique_ptr<Dxr::RaytracingRenderer> m_RaytracingRenderer;
    Dxr::RaytracingScene m_RaytracingScene;

    std::vector<SceneObject> m_SceneObjects;
    std::vector<Dxr::MaterialData> m_Materials;
    std::vector<std::shared_ptr<Texture>> m_Textures;

    std::vector<PointLight> m_PointLights;

    float m_DeltaTime = 0.0f;
    int m_Width = 1;
    int m_Height = 1;

    struct
    {
        float Forward = 0.0f;
        float Backward = 0.0f;
        float Left = 0.0f;
        float Right = 0.0f;
        float Up = 0.0f;
        float Down = 0.0f;
        float Pitch = 0.0f;
        float Yaw = 0.0f;
        bool Shift = false;
    } m_CameraController;
};
