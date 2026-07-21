#pragma once

#include <DX12Library/Camera.h>
#include <DX12Library/Game.h>
#include <DX12Library/StructuredBuffer.h>

#include <Framework/ImGuiImpl.h>
#include <Framework/Light.h>
#include <Framework/Model.h>
#include <Framework/ComputeShader.h>
#include <Framework/RayTracingAccelerationStructure.h>
#include <Framework/RayTracingShader.h>
#include <Framework/Shader.h>

#include <RenderGraph/RenderGraphRoot.h>

#include <memory>
#include <string>
#include <vector>

struct GraphicsSettings;
class CommandList;

namespace RenderGraph
{
    class User;
}

class RaytracingDemo final : public Game
{
public:
    using Base = Game;

    RaytracingDemo(const std::wstring& name, int width, int height, GraphicsSettings graphicsSettings);

    static constexpr uint32_t MaxPathTracingLights = 8;
    static constexpr uint32_t MaxInlineRayTracingTextures = 8;
    static constexpr uint32_t MaxInlineRayTracingGeometryBuffers = 256;

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
    struct MaterialData
    {
        DirectX::XMFLOAT4 Diffuse = { 1.0f, 1.0f, 1.0f, 1.0f };
        DirectX::XMFLOAT4 Specular = { 0.04f, 0.04f, 0.04f, 1.0f };
        DirectX::XMFLOAT4 TilingOffset = { 1.0f, 1.0f, 0.0f, 0.0f };
        uint32_t DiffuseTextureIndex = 0;
        float Metallic = 0.0f;
        float Roughness = 0.5f;
        uint32_t Padding0 = 0;
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
        uint32_t AccumulationFrameIndex = 0;
        uint32_t Padding = 0;
        PathTracingLightData Lights[MaxPathTracingLights] = {};
    };

    struct PipelineConstants
    {
        DirectX::XMMATRIX View = DirectX::XMMatrixIdentity();
        DirectX::XMMATRIX Projection = DirectX::XMMatrixIdentity();
        DirectX::XMMATRIX ViewProjection = DirectX::XMMatrixIdentity();
        DirectX::XMFLOAT4 CameraPosition = { 0.0f, 0.0f, 0.0f, 1.0f };
        DirectX::XMMATRIX InverseView = DirectX::XMMatrixIdentity();
        DirectX::XMMATRIX InverseProjection = DirectX::XMMatrixIdentity();
        DirectX::XMFLOAT2 ScreenResolution = { 1.0f, 1.0f };
        DirectX::XMFLOAT2 ScreenTexelSize = { 1.0f, 1.0f };
    };

    struct ModelConstants
    {
        DirectX::XMMATRIX Model = DirectX::XMMatrixIdentity();
        DirectX::XMMATRIX ModelViewProjection = DirectX::XMMatrixIdentity();
        DirectX::XMMATRIX InverseTransposeModel = DirectX::XMMatrixIdentity();
        DirectX::XMMATRIX Padding = DirectX::XMMatrixIdentity();
    };

    struct GBufferMaterialConstants
    {
        DirectX::XMFLOAT4 Diffuse = { 1.0f, 1.0f, 1.0f, 1.0f };
        DirectX::XMFLOAT4 Specular = { 0.04f, 0.04f, 0.04f, 1.0f };
        DirectX::XMFLOAT4 TilingOffset = { 1.0f, 1.0f, 0.0f, 0.0f };
        float Metallic = 0.0f;
        float Roughness = 0.5f;
        DirectX::XMFLOAT2 Padding = { 0.0f, 0.0f };
    };

    struct SceneObject
    {
        DirectX::XMMATRIX WorldMatrix = DirectX::XMMatrixIdentity();
        std::shared_ptr<Model> Model;
        uint32_t MaterialIndex = 0;
    };

    enum class RayTracingExecutionMode
    {
        StandardDxr = 0,
        InlineRayTracing = 1,
    };

    uint32_t AddTexture(CommandList& commandList, const std::wstring& path, TextureUsageType usage = TextureUsageType::Albedo);
    uint32_t AddMaterial(const MaterialData& material);
    uint32_t AddDiffuseMaterial(
        const DirectX::XMFLOAT4& diffuse,
        const DirectX::XMFLOAT4& tilingOffset,
        uint32_t diffuseTextureIndex,
        float metallic = 0.0f,
        float roughness = 0.5f);
    void LoadDeferredLightingScene(CommandList& commandList);
    void AddRaytracingInstances();
    void BindRayTracingShaderResources();
    CameraConstants BuildCameraConstants() const;
    PipelineConstants BuildPipelineConstants() const;
    void ResetAccumulation();
    void OnImGui();

    Camera m_Camera;
    friend class RenderGraph::User;
    std::unique_ptr<RenderGraph::RenderGraphRoot> m_RenderGraph;
    std::unique_ptr<RayTracingShader> m_RayTracingShader;
    std::unique_ptr<ComputeShader> m_InlineRayTracingShader;
    RayTracingAccelerationStructure m_RayTracingAccelerationStructure;
    StructuredBuffer m_MaterialBuffer;
    StructuredBuffer m_GeometryBuffer;
    std::shared_ptr<CommonRootSignature> m_RootSignature;
    std::unique_ptr<ImGuiImpl> m_ImGui;
    std::shared_ptr<Mesh> m_SkyboxMesh;
    std::shared_ptr<Shader> m_GBufferShader;
    std::shared_ptr<Shader> m_SkyboxShader;
    std::shared_ptr<Texture> m_SkyboxTexture;

    std::vector<SceneObject> m_SceneObjects;
    std::vector<MaterialData> m_Materials;
    std::vector<std::shared_ptr<Texture>> m_Textures;

    std::vector<PointLight> m_PointLights;

    float m_DeltaTime = 0.0f;
    uint32_t m_FrameIndex = 0;
    uint32_t m_AccumulationFrameIndex = 0;
    int m_MaxBounces = 5;
    float m_CameraFov = 45.0f;
    float m_MouseRotateSpeed = 0.1f;
    float m_MousePanSpeed = 0.04f;
    float m_MouseDollySpeed = 0.04f;
    float m_MouseWheelDollySpeed = 0.5f;
    RayTracingExecutionMode m_RayTracingExecutionMode = RayTracingExecutionMode::StandardDxr;
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
