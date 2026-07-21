#pragma once

#include <DX12Library/Camera.h>
#include <DX12Library/Game.h>
#include <DX12Library/StructuredBuffer.h>

#include <Framework/Light.h>
#include <Framework/Model.h>
#include <Framework/RayTracingAccelerationStructure.h>
#include <Framework/RayTracingShader.h>
#include <Framework/TAA.h>

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
    static constexpr uint32_t MaxPathTracingLights = 8;

    struct MaterialData
    {
        DirectX::XMFLOAT4 Diffuse = { 1.0f, 1.0f, 1.0f, 1.0f };
        DirectX::XMFLOAT4 TilingOffset = { 1.0f, 1.0f, 0.0f, 0.0f };
        uint32_t DiffuseTextureIndex = 0;
        uint32_t Padding0 = 0;
        uint32_t Padding1 = 0;
        uint32_t Padding2 = 0;
    };

    struct PathTracingLightData
    {
        DirectX::XMFLOAT4 PositionAndRange = { 0.0f, 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT4 ColorAndIntensity = { 0.0f, 0.0f, 0.0f, 1.0f };
        DirectX::XMFLOAT4 Attenuation = { 1.0f, 0.0f, 0.0f, 0.0f };
    };

    struct CameraConstants
    {
        DirectX::XMMATRIX InverseView = DirectX::XMMatrixIdentity();
        DirectX::XMMATRIX InverseProjection = DirectX::XMMatrixIdentity();
        DirectX::XMFLOAT4 CameraPosition = { 0.0f, 0.0f, 0.0f, 1.0f };
        uint32_t Width = 1;
        uint32_t Height = 1;
        uint32_t MaxBounces = 5;
        uint32_t SamplesPerPixel = 1;
        uint32_t LightCount = 0;
        uint32_t FrameIndex = 0;
        DirectX::XMFLOAT2 TaaJitterOffset = { 0.0f, 0.0f };
        PathTracingLightData Lights[MaxPathTracingLights] = {};
    };

    struct SceneObject
    {
        DirectX::XMMATRIX WorldMatrix = DirectX::XMMatrixIdentity();
        std::shared_ptr<Model> Model;
        uint32_t MaterialIndex = 0;
    };

    uint32_t AddTexture(CommandList& commandList, const std::wstring& path, TextureUsageType usage = TextureUsageType::Albedo);
    uint32_t AddMaterial(const MaterialData& material);
    uint32_t AddDiffuseMaterial(
        const DirectX::XMFLOAT4& diffuse,
        const DirectX::XMFLOAT4& tilingOffset,
        uint32_t diffuseTextureIndex);
    void LoadDeferredLightingScene(CommandList& commandList);
    std::vector<RayTracingMeshInstance> CreateRaytracingInstances() const;
    void ResizeRayTracingOutputTexture();

    Camera m_Camera;
    std::unique_ptr<RayTracingShader> m_RayTracingShader;
    RayTracingAccelerationStructure m_RayTracingAccelerationStructure;
    std::shared_ptr<Texture> m_RayTracingOutputTexture;
    StructuredBuffer m_MaterialBuffer;
    StructuredBuffer m_GeometryBuffer;
    std::shared_ptr<CommonRootSignature> m_RootSignature;
    std::unique_ptr<TAA> m_Taa;
    std::shared_ptr<Texture> m_TaaVelocityTexture;

    std::vector<SceneObject> m_SceneObjects;
    std::vector<MaterialData> m_Materials;
    std::vector<std::shared_ptr<Texture>> m_Textures;

    std::vector<PointLight> m_PointLights;

    float m_DeltaTime = 0.0f;
    uint32_t m_FrameIndex = 0;
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
