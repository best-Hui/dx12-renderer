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
#include <Framework/SharedUploadBuffer.h>

#include <Passes/NrdPass.h>
#include <Passes/SvgfPass.h>
#include <RenderGraph/RenderGraphRoot.h>
#include <Scene/SceneLighting.h>

#include <memory>
#include <string>
#include <vector>

struct GraphicsSettings;
class CommandList;
namespace RenderGraph
{
    class User;
}

namespace RaytracingDemoPasses
{
    class Builder;
}

class RaytracingDemo final : public Game
{
public:
    using Base = Game;

    RaytracingDemo(const std::wstring& name, int width, int height, GraphicsSettings graphicsSettings);

    static constexpr uint32_t MinRayTracingDescriptorArrayCapacity = 1;

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
        uint32_t NormalTextureIndex = 0;
        uint32_t MetallicTextureIndex = 0;
        uint32_t RoughnessTextureIndex = 0;
        uint32_t AmbientOcclusionTextureIndex = 0;
        uint32_t HasDiffuseMap = 0;
        uint32_t HasNormalMap = 0;
        uint32_t HasMetallicMap = 0;
        uint32_t HasRoughnessMap = 0;
        uint32_t HasAmbientOcclusionMap = 0;
        float Metallic = 0.0f;
        float Roughness = 0.5f;
        uint32_t Padding0 = 0;
        uint32_t Padding1 = 0;
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
        uint32_t DirectionalLightCount = 0;
        uint32_t PointLightCount = 0;
        uint32_t AreaLightCount = 0;
        uint32_t FrameIndex = 0;
        uint32_t AccumulationFrameIndex = 0;
        uint32_t AccumulationEnabled = 1;
        uint32_t NrdDenoiserMode = 0;
        float NrdReblurHitDistanceScale = 100.0f;
        uint32_t Padding0 = 0;
        uint32_t Padding1 = 0;
        SkyLightData SkyLight = {};
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

    struct LightBillboardConstants
    {
        DirectX::XMFLOAT4 PositionAndSize = { 0.0f, 0.0f, 0.0f, 1.0f };
        DirectX::XMFLOAT4 ColorAndAlpha = { 1.0f, 1.0f, 1.0f, 0.45f };
        DirectX::XMFLOAT4 CameraRight = { 1.0f, 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT4 CameraUp = { 0.0f, 1.0f, 0.0f, 0.0f };
    };

    struct GBufferMaterialConstants
    {
        DirectX::XMFLOAT4 Diffuse = { 1.0f, 1.0f, 1.0f, 1.0f };
        DirectX::XMFLOAT4 Specular = { 0.04f, 0.04f, 0.04f, 1.0f };
        DirectX::XMFLOAT4 TilingOffset = { 1.0f, 1.0f, 0.0f, 0.0f };
        float Metallic = 0.0f;
        float Roughness = 0.5f;
        uint32_t HasDiffuseMap = 0;
        uint32_t HasNormalMap = 0;
        uint32_t HasMetallicMap = 0;
        uint32_t HasRoughnessMap = 0;
        uint32_t HasAmbientOcclusionMap = 0;
        uint32_t Padding0 = 0;
        uint32_t Padding1 = 0;
        uint32_t Padding2 = 0;
    };

    struct SceneObject
    {
        DirectX::XMMATRIX WorldMatrix = DirectX::XMMatrixIdentity();
        std::shared_ptr<Model> Model;
        uint32_t MaterialIndex = 0;
    };

    enum class PathTracingBackend
    {
        InlineRayQuery = 0,
        ShaderTableDxr = 1,
    };

    enum class DenoiserAlgorithm
    {
        Off = 0,
        Nrd = 1,
        Svgf = 2,
    };

    struct RayTracingSceneResourceLayout
    {
        uint32_t TextureDescriptorCapacity = MinRayTracingDescriptorArrayCapacity;
        uint32_t GeometryDescriptorCapacity = MinRayTracingDescriptorArrayCapacity;

        bool operator!=(const RayTracingSceneResourceLayout& other) const
        {
            return TextureDescriptorCapacity != other.TextureDescriptorCapacity ||
                GeometryDescriptorCapacity != other.GeometryDescriptorCapacity;
        }
    };

    uint32_t AddTexture(CommandList& commandList, const std::wstring& path, TextureUsageType usage = TextureUsageType::Albedo);
    uint32_t AddMaterial(const MaterialData& material);
    uint32_t AddPbrMaterial(
        const DirectX::XMFLOAT4& diffuse,
        const DirectX::XMFLOAT4& tilingOffset,
        uint32_t diffuseTextureIndex,
        uint32_t normalTextureIndex,
        uint32_t metallicTextureIndex,
        uint32_t roughnessTextureIndex,
        uint32_t ambientOcclusionTextureIndex,
        float metallic = 0.0f,
        float roughness = 0.5f,
        bool hasDiffuseMap = true,
        bool hasNormalMap = false,
        bool hasMetallicMap = false,
        bool hasRoughnessMap = false,
        bool hasAmbientOcclusionMap = false);
    uint32_t AddDiffuseMaterial(
        const DirectX::XMFLOAT4& diffuse,
        const DirectX::XMFLOAT4& tilingOffset,
        uint32_t diffuseTextureIndex,
        float metallic = 0.0f,
        float roughness = 0.5f);
    void LoadDeferredLightingScene(CommandList& commandList);
    void CreateDemoLights();
    void AddPointLightAtOrigin();
    void AddRandomPointLightInUpperHemisphere();
    void UpdateDynamicLights(float timeSeconds);
    void InitializeSceneLightBuffers(CommandList& commandList);
    void BuildSceneLightGpuData();
    void UpdatePointLightGpuData(size_t lightIndex);
    void MarkDirectionalLightsDirty();
    void MarkDirectionalLightsDirty(size_t beginIndex, size_t endIndex);
    void MarkPointLightsDirty(size_t beginIndex, size_t endIndex);
    void MarkAreaLightsDirty();
    void MarkAreaLightsDirty(size_t beginIndex, size_t endIndex);
    void UploadSceneLightBuffers(CommandList& commandList);
    void AddRaytracingInstances();
    RayTracingSceneResourceLayout BuildRayTracingSceneResourceLayout() const;
    void EnsureRayTracingPipelines();
    void BindRayTracingShaderResources();
    CameraConstants BuildCameraConstants() const;
    PipelineConstants BuildPipelineConstants() const;
    void ResetAccumulation(bool resetDenoiserHistory = true);
    bool IsDenoiserEnabled() const { return m_DenoiserAlgorithm != DenoiserAlgorithm::Off; }
    bool IsNrdDenoiserEnabled() const { return m_DenoiserAlgorithm == DenoiserAlgorithm::Nrd; }
    bool IsSvgfDenoiserEnabled() const { return m_DenoiserAlgorithm == DenoiserAlgorithm::Svgf; }
    void ApplyDenoiserSelection();
    void OnImGui();

    Camera m_Camera;
    friend class RenderGraph::User;
    friend class RaytracingDemoPasses::Builder;
    friend class NrdPass;
    friend class SvgfPass;
    std::unique_ptr<RenderGraph::RenderGraphRoot> m_RenderGraph;
    std::unique_ptr<RayTracingShader> m_RayTracingShader;
    std::unique_ptr<ComputeShader> m_InlinePathTracingShader;
    std::unique_ptr<NrdPass> m_NrdPass;
    std::unique_ptr<SvgfPass> m_SvgfPass;
    RayTracingAccelerationStructure m_RayTracingAccelerationStructure;
    StructuredBuffer m_MaterialBuffer;
    StructuredBuffer m_GeometryBuffer;
    StructuredBuffer m_DirectionalLightBuffer;
    StructuredBuffer m_PointLightBuffer;
    StructuredBuffer m_AreaLightBuffer;
    std::unique_ptr<SharedUploadBuffer> m_LightUploadBuffer;
    std::shared_ptr<CommonRootSignature> m_RootSignature;
    std::unique_ptr<ImGuiImpl> m_ImGui;
    std::shared_ptr<Mesh> m_SkyboxMesh;
    std::shared_ptr<Mesh> m_LightBillboardMesh;
    std::shared_ptr<Shader> m_GBufferShader;
    std::shared_ptr<Shader> m_SkyboxShader;
    std::shared_ptr<Shader> m_LightBillboardShader;
    std::shared_ptr<Texture> m_SkyboxTexture;

    std::vector<SceneObject> m_SceneObjects;
    std::vector<MaterialData> m_Materials;
    std::vector<std::shared_ptr<Texture>> m_Textures;
    RayTracingSceneResourceLayout m_RayTracingSceneResourceLayout;

    SkyLightData m_SkyLight;
    std::vector<DirectionalLight> m_DirectionalLights;
    std::vector<PointLight> m_PointLights;
    std::vector<AreaLightData> m_AreaLights;
    std::vector<DirectionalLightData> m_DirectionalLightGpuData;
    std::vector<PointLightData> m_PointLightGpuData;
    std::vector<AreaLightData> m_AreaLightGpuData;
    size_t m_DirectionalLightBufferCapacity = 0;
    size_t m_PointLightBufferCapacity = 0;
    size_t m_AreaLightBufferCapacity = 0;
    std::vector<float> m_PointLightBaseY;
    std::vector<float> m_PointLightPhase;
    std::vector<float> m_PointLightOrbitRadius;
    std::vector<float> m_PointLightOrbitSpeed;
    std::vector<DirectX::XMFLOAT3> m_PointLightOrbitCenter;
    std::vector<uint8_t> m_PointLightAnimated;
    size_t m_DirectionalLightDirtyBegin = 0;
    size_t m_DirectionalLightDirtyEnd = 0;
    size_t m_PointLightDirtyBegin = 0;
    size_t m_PointLightDirtyEnd = 0;
    size_t m_AreaLightDirtyBegin = 0;
    size_t m_AreaLightDirtyEnd = 0;
    DirectX::XMFLOAT3 m_NewPointLightColor = { 1.0f, 0.85f, 0.55f };
    float m_NewPointLightIntensity = 18.0f;
    float m_NewPointLightRange = 24.0f;
    float m_RandomPointLightSpawnRadius = 28.0f;

    float m_DeltaTime = 0.0f;
    uint32_t m_FrameIndex = 0;
    uint32_t m_AccumulationFrameIndex = 0;
    int m_MaxBounces = 1;
    bool m_AccumulationEnabled = true;
    DenoiserAlgorithm m_DenoiserAlgorithm = DenoiserAlgorithm::Off;
    bool m_AnimatePointLights = false;
    float m_CameraFov = 45.0f;
    float m_MouseRotateSpeed = 0.1f;
    float m_MousePanSpeed = 0.04f;
    float m_MouseDollySpeed = 0.04f;
    float m_MouseWheelDollySpeed = 0.5f;
    PathTracingBackend m_PathTracingBackend = PathTracingBackend::InlineRayQuery;
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
