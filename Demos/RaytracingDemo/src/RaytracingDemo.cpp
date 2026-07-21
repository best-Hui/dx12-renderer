#include <RaytracingDemo.h>

#include <DX12Library/Application.h>
#include <DX12Library/CommandList.h>
#include <DX12Library/CommandQueue.h>
#include <DX12Library/Events.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/Window.h>

#include <Framework/GraphicsSettings.h>
#include <Framework/Mesh.h>
#include <Framework/ModelLoader.h>
#include <Framework/PipelineStateBuilder.h>
#include <Framework/ShaderBlob.h>

#include <RenderGraph/RenderMetadata.h>

#include <DirectXMath.h>
#include <d3dx12.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "RenderGraph.User.h"

#if defined(min)
#undef min
#endif

#if defined(max)
#undef max
#endif

using namespace DirectX;

namespace
{
    template<typename T>
    constexpr const T& Clamp(const T& val, const T& min, const T& max)
    {
        return val < min ? min : val > max ? max : val;
    }

}

RaytracingDemo::RaytracingDemo(const std::wstring& name, const int width, const int height, GraphicsSettings graphicsSettings)
    : Base(name, width, height, false)
    , m_MaterialBuffer(L"Ray Tracing Materials")
    , m_GeometryBuffer(L"Ray Tracing Geometry Data")
    , m_Width(width)
    , m_Height(height)
{
    (void)graphicsSettings;

    const XMVECTOR cameraPos = XMVectorSet(0, 8, -35, 1);
    const XMVECTOR cameraTarget = XMVectorSet(0, 5, 18, 1);
    const XMVECTOR cameraUp = XMVectorSet(0, 1, 0, 0);
    m_Camera.SetLookAt(cameraPos, cameraTarget, cameraUp);
    m_Camera.SetProjection(m_CameraFov, static_cast<float>(m_Width) / static_cast<float>(m_Height), 0.1f, 1000.0f);

    char* mode = nullptr;
    size_t modeLength = 0;
    _dupenv_s(&mode, &modeLength, "RAYTRACING_DEMO_MODE");
    if (mode != nullptr && std::strcmp(mode, "inline") == 0)
    {
        m_RayTracingExecutionMode = RayTracingExecutionMode::InlineRayTracing;
    }
    std::free(mode);
}

bool RaytracingDemo::LoadContent()
{
    Assert(RayTracingShader::IsSupported(), "DirectX Raytracing is not supported by the selected adapter.");

    const auto commandQueue = Application::Get().GetCommandQueue();
    const auto commandList = commandQueue->GetCommandList();

    LoadDeferredLightingScene(*commandList);

    m_SkyboxTexture = std::make_shared<Texture>();
    commandList->LoadTextureFromFile(*m_SkyboxTexture, L"Assets/Textures/skybox/skybox.dds", TextureUsageType::Albedo);

    Assert(!m_Textures.empty(), "RaytracingDemo needs at least one texture before creating the common root signature.");
    m_RootSignature = std::make_shared<CommonRootSignature>(m_Textures.front());
    m_ImGui = std::make_unique<ImGuiImpl>(*commandList, *PWindow, m_RootSignature);

    m_SkyboxMesh = Mesh::CreateCube(*commandList);

    m_GBufferShader = std::make_shared<Shader>(
        m_RootSignature,
        ShaderBlob(L"RaytracingDemo_GBuffer_VS.cso"),
        ShaderBlob(L"RaytracingDemo_GBuffer_PS.cso"),
        [](PipelineStateBuilder&) {},
        false);

    m_SkyboxShader = std::make_shared<Shader>(
        m_RootSignature,
        ShaderBlob(L"RaytracingDemo_Skybox_VS.cso"),
        ShaderBlob(L"RaytracingDemo_Skybox_PS.cso"),
        [](PipelineStateBuilder& builder)
        {
            auto rasterizer = CD3DX12_RASTERIZER_DESC(D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_FRONT, FALSE, 0, 0,
                0, TRUE, FALSE, FALSE, 0, D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF);
            auto depthStencil = CD3DX12_DEPTH_STENCIL_DESC(CD3DX12_DEFAULT());
            depthStencil.DepthEnable = true;
            depthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

            builder.WithRasterizer(rasterizer).WithDepthStencil(depthStencil);
        },
        false);

    RayTracingPipelineDesc rayTracingDesc = RayTracingShader::CreateDefaultPipelineDesc();
    rayTracingDesc.Bindings.push_back({ "GBufferTextures", RayTracingShaderBindingType::TextureArray, 0, 4, 4 });
    rayTracingDesc.Bindings.push_back({ "Skybox", RayTracingShaderBindingType::TextureArray, 0, 5, 1 });
    rayTracingDesc.Bindings.push_back({ "Accumulation", RayTracingShaderBindingType::OutputTexture, 1, 0, 1 });
    m_RayTracingShader = std::make_unique<RayTracingShader>(ShaderBlob(L"RaytracingDemo_RT.cso"), rayTracingDesc);
    m_InlineRayTracingShader = std::make_unique<ComputeShader>(m_RootSignature, ShaderBlob(L"RaytracingDemo_InlineRT_CS.cso"));

    AddRaytracingInstances();

    RayTracingAccelerationStructureBuildSettings accelerationStructureSettings{};
    accelerationStructureSettings.AllowUpdate = true;
    m_RayTracingAccelerationStructure.Build(*commandList, accelerationStructureSettings);
    commandList->CopyStructuredBuffer(m_MaterialBuffer, m_Materials);
    commandList->CopyStructuredBuffer(m_GeometryBuffer, m_RayTracingAccelerationStructure.GetGeometryData());
    BindRayTracingShaderResources();

    m_RenderGraph = RenderGraph::User::Create(*this, *commandList);

    const uint64_t fenceValue = commandQueue->ExecuteCommandList(commandList);
    commandQueue->WaitForFenceValue(fenceValue);
    return true;
}

void RaytracingDemo::UnloadContent()
{
    m_RenderGraph.reset();
    m_SkyboxShader.reset();
    m_GBufferShader.reset();
    m_SkyboxMesh.reset();
    m_ImGui.reset();
    m_RootSignature.reset();
    m_SkyboxTexture.reset();
    m_InlineRayTracingShader.reset();
    m_RayTracingShader.reset();
    m_SceneObjects.clear();
    m_Materials.clear();
    m_Textures.clear();
}

void RaytracingDemo::BindRayTracingShaderResources()
{
    m_RayTracingShader->SetAccelerationStructure("Scene", m_RayTracingAccelerationStructure);
    m_RayTracingShader->SetStructuredBuffer("Materials", m_MaterialBuffer);
    m_RayTracingShader->SetStructuredBuffer("Geometries", m_GeometryBuffer);
    m_RayTracingShader->SetTextureArray("Textures", m_Textures);

    D3D12_SHADER_RESOURCE_VIEW_DESC skyboxSrvDesc = {};
    skyboxSrvDesc.Format = m_SkyboxTexture->GetD3D12ResourceDesc().Format;
    skyboxSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    skyboxSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    skyboxSrvDesc.TextureCube.MipLevels = 1;
    skyboxSrvDesc.TextureCube.MostDetailedMip = 0;
    skyboxSrvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
    m_RayTracingShader->SetTextureArray("Skybox", { m_SkyboxTexture }, { skyboxSrvDesc });
}

RaytracingDemo::CameraConstants RaytracingDemo::BuildCameraConstants() const
{
    CameraConstants camera{};
    camera.InverseView = XMMatrixInverse(nullptr, m_Camera.GetViewMatrix());
    camera.InverseProjection = XMMatrixInverse(nullptr, m_Camera.GetProjectionMatrix());
    XMStoreFloat4(&camera.CameraPosition, m_Camera.GetTranslation());
    camera.Width = static_cast<uint32_t>(m_Width);
    camera.Height = static_cast<uint32_t>(m_Height);
    camera.MaxBounces = static_cast<uint32_t>(Clamp(m_MaxBounces, 0, 5));
    camera.SamplesPerPixel = 1;
    camera.LightCount = static_cast<uint32_t>(std::min<size_t>(m_PointLights.size(), MaxPathTracingLights));
    camera.FrameIndex = m_FrameIndex;
    camera.AccumulationFrameIndex = m_AccumulationFrameIndex;

    for (uint32_t i = 0; i < camera.LightCount; ++i)
    {
        const PointLight& pointLight = m_PointLights[i];
        camera.Lights[i].PositionAndRange = {
            pointLight.PositionWs.x,
            pointLight.PositionWs.y,
            pointLight.PositionWs.z,
            pointLight.Range
        };
        camera.Lights[i].ColorAndIntensity = {
            pointLight.Color.x,
            pointLight.Color.y,
            pointLight.Color.z,
            20.0f
        };
        camera.Lights[i].Attenuation = {
            pointLight.ConstantAttenuation,
            pointLight.LinearAttenuation,
            pointLight.QuadraticAttenuation,
            0.0f
        };
    }

    return camera;
}

RaytracingDemo::PipelineConstants RaytracingDemo::BuildPipelineConstants() const
{
    PipelineConstants pipeline{};
    pipeline.View = m_Camera.GetViewMatrix();
    pipeline.Projection = m_Camera.GetProjectionMatrix();
    pipeline.ViewProjection = pipeline.View * pipeline.Projection;
    XMStoreFloat4(&pipeline.CameraPosition, m_Camera.GetTranslation());
    pipeline.InverseView = XMMatrixInverse(nullptr, pipeline.View);
    pipeline.InverseProjection = XMMatrixInverse(nullptr, pipeline.Projection);
    pipeline.ScreenResolution = { static_cast<float>(m_Width), static_cast<float>(m_Height) };
    pipeline.ScreenTexelSize = { 1.0f / pipeline.ScreenResolution.x, 1.0f / pipeline.ScreenResolution.y };
    return pipeline;
}

void RaytracingDemo::ResetAccumulation()
{
    m_AccumulationFrameIndex = 0;
}

void RaytracingDemo::OnImGui()
{
    ImGui::Begin("Raytracing");
    ImGui::Text("DXR GBuffer Path Tracing");
    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
    ImGui::Text("Resolution: %d x %d", m_Width, m_Height);
    ImGui::Text("Frame: %u", m_FrameIndex);
    ImGui::Text("Accumulation: %u", m_AccumulationFrameIndex);

    const char* modeNames[] = { "Standard DXR", "Inline RayQuery CS" };
    int selectedMode = static_cast<int>(m_RayTracingExecutionMode);
    if (ImGui::Combo("Mode", &selectedMode, modeNames, 2))
    {
        m_RayTracingExecutionMode = static_cast<RayTracingExecutionMode>(selectedMode);
        ResetAccumulation();
    }

    const bool bouncesChanged = ImGui::SliderInt("Bounces", &m_MaxBounces, 0, 5);
    const bool fovChanged = ImGui::SliderFloat("FOV", &m_CameraFov, 12.0f, 90.0f, "%.1f");
    const bool rotateSpeedChanged = ImGui::SliderFloat("Mouse Rotate", &m_MouseRotateSpeed, 0.01f, 0.5f, "%.3f");
    const bool panSpeedChanged = ImGui::SliderFloat("Mouse Pan", &m_MousePanSpeed, 0.005f, 0.25f, "%.3f");
    const bool dollySpeedChanged = ImGui::SliderFloat("Mouse Dolly", &m_MouseDollySpeed, 0.005f, 0.25f, "%.3f");
    const bool wheelSpeedChanged = ImGui::SliderFloat("Wheel Dolly", &m_MouseWheelDollySpeed, 0.05f, 5.0f, "%.2f");

    if (bouncesChanged)
    {
        ResetAccumulation();
    }
    if (fovChanged)
    {
        const float aspectRatio = static_cast<float>(m_Width) / static_cast<float>(m_Height);
        m_Camera.SetProjection(m_CameraFov, aspectRatio, 0.1f, 1000.0f);
        ResetAccumulation();
    }
    if (rotateSpeedChanged || panSpeedChanged || dollySpeedChanged || wheelSpeedChanged)
    {
        ResetAccumulation();
    }
    ImGui::End();
}

uint32_t RaytracingDemo::AddTexture(CommandList& commandList, const std::wstring& path, TextureUsageType usage)
{
    auto texture = std::make_shared<Texture>();
    commandList.LoadTextureFromFile(*texture, path, usage);
    m_Textures.push_back(texture);
    return static_cast<uint32_t>(m_Textures.size() - 1);
}

uint32_t RaytracingDemo::AddMaterial(const MaterialData& material)
{
    m_Materials.push_back(material);
    return static_cast<uint32_t>(m_Materials.size() - 1);
}

uint32_t RaytracingDemo::AddDiffuseMaterial(
    const XMFLOAT4& diffuse,
    const XMFLOAT4& tilingOffset,
    const uint32_t diffuseTextureIndex,
    const float metallic,
    const float roughness)
{
    MaterialData material{};
    material.Diffuse = diffuse;
    material.Specular = { 0.04f, 0.04f, 0.04f, 1.0f };
    material.TilingOffset = tilingOffset;
    material.DiffuseTextureIndex = diffuseTextureIndex;
    material.Metallic = metallic;
    material.Roughness = roughness;
    return AddMaterial(material);
}

void RaytracingDemo::LoadDeferredLightingScene(CommandList& commandList)
{
    ModelLoader modelLoader;
    m_PointLights.clear();

    const uint32_t whiteTexture = AddTexture(commandList, L"Assets/Textures/white.png");
    const uint32_t groundTexture = AddTexture(commandList, L"Assets/Textures/Ground047/Ground047_1K_Color.jpg");
    const uint32_t chestTexture = AddTexture(commandList, L"Assets/Models/old-wooden-chest/chest_01_BaseColor.png");
    const uint32_t cerberusTexture = AddTexture(commandList, L"Assets/Models/cerberus/Cerberus_A.jpg");
    const uint32_t tvTexture = AddTexture(commandList, L"Assets/Models/tv/TV_Color.jpg");

    const uint32_t groundMaterial = AddDiffuseMaterial({ 1, 1, 1, 1 }, { 6, 6, 0, 0 }, groundTexture, 0.0f, 0.85f);
    const uint32_t chestMaterial = AddDiffuseMaterial({ 1, 1, 1, 1 }, { 1, 1, 0, 0 }, chestTexture, 0.0f, 0.55f);
    const uint32_t mirrorMaterial = AddDiffuseMaterial({ 0.85f, 0.85f, 0.92f, 1 }, { 1, 1, 0, 0 }, whiteTexture, 1.0f, 0.08f);
    const uint32_t cubeMaterial = AddDiffuseMaterial({ 0.9f, 0.9f, 0.9f, 1 }, { 1, 1, 0, 0 }, whiteTexture, 0.0f, 0.35f);
    const uint32_t cerberusMaterial = AddDiffuseMaterial({ 1, 1, 1, 1 }, { 1, 1, 0, 0 }, cerberusTexture, 0.0f, 0.45f);
    const uint32_t tvMaterial = AddDiffuseMaterial({ 1, 1, 1, 1 }, { 1, 1, 0, 0 }, tvTexture, 0.2f, 0.35f);

    {
        auto model = modelLoader.LoadExisting(Mesh::CreatePlane(commandList));
        XMMATRIX worldMatrix = XMMatrixScaling(200.0f, 200.0f, 200.0f);
        m_SceneObjects.push_back({ worldMatrix, model, groundMaterial });
    }

    {
        auto model = modelLoader.Load(commandList, "Assets/Models/old-wooden-chest/chest_01.fbx");

        XMMATRIX worldMatrix =
            XMMatrixScaling(0.01f, 0.01f, 0.01f) *
            XMMatrixRotationRollPitchYaw(XMConvertToRadians(90.0f), 0.0f, 0.0f) *
            XMMatrixTranslation(0.0f, 0.25f, 15.0f);
        m_SceneObjects.push_back({ worldMatrix, model, chestMaterial });

        worldMatrix =
            XMMatrixScaling(0.01f, 0.01f, 0.01f) *
            XMMatrixRotationRollPitchYaw(XMConvertToRadians(90.0f), 0.0f, 0.0f) *
            XMMatrixTranslation(-50.0f, 0.25f, 15.0f);
        m_SceneObjects.push_back({ worldMatrix, model, chestMaterial });
    }

    {
        auto model = modelLoader.LoadExisting(Mesh::CreatePlane(commandList));
        XMMATRIX worldMatrix = XMMatrixScaling(30.0f, 30.0f, 30.0f) * XMMatrixTranslation(-50.0f, 0.1f, 15.0f);
        m_SceneObjects.push_back({ worldMatrix, model, mirrorMaterial });
    }

    {
        auto model = modelLoader.LoadExisting(Mesh::CreateCube(commandList));
        XMMATRIX worldMatrix = XMMatrixScaling(5.0f, 5.0f, 5.0f) * XMMatrixTranslation(-54.0f, 2.5f, 7.0f);
        m_SceneObjects.push_back({ worldMatrix, model, cubeMaterial });
    }

    {
        auto model = modelLoader.Load(commandList, "Assets/Models/cerberus/Cerberus_LP.FBX");
        XMMATRIX worldMatrix =
            XMMatrixScaling(0.10f, 0.10f, 0.10f) *
            XMMatrixRotationRollPitchYaw(XMConvertToRadians(90.0f), XMConvertToRadians(135.0f), 0.0f) *
            XMMatrixTranslation(15.0f, 5.0f, 10.0f);
        m_SceneObjects.push_back({ worldMatrix, model, cerberusMaterial });
    }

    {
        auto model = modelLoader.Load(commandList, "Assets/Models/tv/TV.FBX");
        XMMATRIX worldMatrix =
            XMMatrixScaling(0.30f, 0.30f, 0.30f) *
            XMMatrixRotationRollPitchYaw(XMConvertToRadians(90.0f), XMConvertToRadians(-45.0f), 0.0f) *
            XMMatrixTranslation(-14.0f, 0.0f, 18.0f);
        m_SceneObjects.push_back({ worldMatrix, model, tvMaterial });
    }

    const int steps = 5;
    for (int x = 0; x < steps; ++x)
    {
        for (int y = 0; y < steps; ++y)
        {
            const float metallic = static_cast<float>(x) / static_cast<float>(steps - 1);
            const float roughness = static_cast<float>(y) / static_cast<float>(steps - 1);
            const XMFLOAT4 color = {
                0.25f + metallic * 0.75f,
                0.25f + roughness * 0.75f,
                0.9f - roughness * 0.45f,
                1.0f
            };

            const uint32_t material = AddDiffuseMaterial(color, { 1, 1, 0, 0 }, whiteTexture, metallic, roughness);
            auto model = modelLoader.LoadExisting(Mesh::CreateSphere(commandList));
            XMMATRIX worldMatrix = XMMatrixTranslation(x * 1.5f, 5.0f + y * 2.0f, 25.0f);
            m_SceneObjects.push_back({ worldMatrix, model, material });
        }
    }

    {
        PointLight pointLight(XMFLOAT4(-8, 2, 10, 1));
        pointLight.Color = XMFLOAT4(1.0f, 0.0f, 1.0f, 1.0f);
        m_PointLights.push_back(pointLight);
    }
    {
        PointLight pointLight(XMFLOAT4(-6, 2, 17, 1));
        pointLight.Color = XMFLOAT4(3.0f, 2.0f, 0.25f, 1.0f);
        m_PointLights.push_back(pointLight);
    }
    {
        PointLight pointLight(XMFLOAT4(6, 4, 13, 1), 25.0f);
        pointLight.Color = XMFLOAT4(0.0f, 4.0f, 2.0f, 1.0f);
        m_PointLights.push_back(pointLight);
    }
    {
        PointLight pointLight(XMFLOAT4(10, 4, 18, 1), 25.0f);
        pointLight.Color = XMFLOAT4(4.0f, 0.0f, 0.1f, 1.0f);
        m_PointLights.push_back(pointLight);
    }
    {
        PointLight pointLight(XMFLOAT4(-2, 5, 12, 1));
        pointLight.Color = XMFLOAT4(0.0f, 1.0f, 0.0f, 1.0f);
        m_PointLights.push_back(pointLight);
    }
}

void RaytracingDemo::AddRaytracingInstances()
{
    m_RayTracingAccelerationStructure.ClearInstances();

    for (const SceneObject& object : m_SceneObjects)
    {
        for (const auto& mesh : object.Model->GetMeshes())
        {
            m_RayTracingAccelerationStructure.AddInstance({
                mesh,
                object.WorldMatrix,
                object.MaterialIndex
            });
        }
    }
}

void RaytracingDemo::OnUpdate(UpdateEventArgs& e)
{
    Base::OnUpdate(e);
    m_DeltaTime = static_cast<float>(e.ElapsedTime);

    const float speedMultiplier = m_CameraController.Shift ? 16.0f : 4.0f;
    const float speed = speedMultiplier * m_DeltaTime;
    const bool movedByKeyboard =
        m_CameraController.Right != 0.0f ||
        m_CameraController.Left != 0.0f ||
        m_CameraController.Forward != 0.0f ||
        m_CameraController.Backward != 0.0f ||
        m_CameraController.Up != 0.0f ||
        m_CameraController.Down != 0.0f;
    if (movedByKeyboard)
    {
        ResetAccumulation();
    }

    const XMVECTOR cameraTranslate = XMVectorSet(
        m_CameraController.Right - m_CameraController.Left,
        0.0f,
        m_CameraController.Forward - m_CameraController.Backward,
        1.0f) * speed;
    const XMVECTOR cameraPan = XMVectorSet(
        0.0f,
        m_CameraController.Up - m_CameraController.Down,
        0.0f,
        1.0f) * speed;

    m_Camera.Translate(cameraTranslate, Space::Local);
    m_Camera.Translate(cameraPan, Space::Local);

    const XMVECTOR cameraRotation = XMQuaternionRotationRollPitchYaw(
        XMConvertToRadians(m_CameraController.Pitch),
        XMConvertToRadians(m_CameraController.Yaw),
        0.0f);
    m_Camera.SetRotation(cameraRotation);
}

void RaytracingDemo::OnRender(RenderEventArgs& e)
{
    Base::OnRender(e);

    if (m_ImGui != nullptr)
    {
        m_ImGui->BeginFrame();
        OnImGui();
        m_ImGui->Render();
    }

    RenderGraph::RenderMetadata metadata;
    metadata.m_ScreenWidth = static_cast<uint32_t>(m_Width);
    metadata.m_ScreenHeight = static_cast<uint32_t>(m_Height);
    metadata.m_FrameIndex = m_FrameIndex;
    metadata.m_Time = e.TotalTime;

    m_RenderGraph->Execute(metadata);
    m_RenderGraph->Present(PWindow);

    ++m_FrameIndex;
    ++m_AccumulationFrameIndex;
}

void RaytracingDemo::OnKeyPressed(KeyEventArgs& e)
{
    if (m_ImGui != nullptr && m_ImGui->WantsToCaptureKeyboard())
    {
        return;
    }

    Base::OnKeyPressed(e);

    switch (e.Key)
    {
    case KeyCode::Escape:
        Application::Get().Quit(0);
        break;
    case KeyCode::Up:
    case KeyCode::W:
        m_CameraController.Forward = 1.0f;
        break;
    case KeyCode::Left:
    case KeyCode::A:
        m_CameraController.Left = 1.0f;
        break;
    case KeyCode::Down:
    case KeyCode::S:
        m_CameraController.Backward = 1.0f;
        break;
    case KeyCode::Right:
    case KeyCode::D:
        m_CameraController.Right = 1.0f;
        break;
    case KeyCode::Q:
        m_CameraController.Down = 1.0f;
        break;
    case KeyCode::E:
        m_CameraController.Up = 1.0f;
        break;
    case KeyCode::ShiftKey:
        m_CameraController.Shift = true;
        break;
    }
}

void RaytracingDemo::OnKeyReleased(KeyEventArgs& e)
{
    if (m_ImGui != nullptr && m_ImGui->WantsToCaptureKeyboard())
    {
        return;
    }

    Base::OnKeyReleased(e);

    switch (e.Key)
    {
    case KeyCode::Up:
    case KeyCode::W:
        m_CameraController.Forward = 0.0f;
        break;
    case KeyCode::Left:
    case KeyCode::A:
        m_CameraController.Left = 0.0f;
        break;
    case KeyCode::Down:
    case KeyCode::S:
        m_CameraController.Backward = 0.0f;
        break;
    case KeyCode::Right:
    case KeyCode::D:
        m_CameraController.Right = 0.0f;
        break;
    case KeyCode::Q:
        m_CameraController.Down = 0.0f;
        break;
    case KeyCode::E:
        m_CameraController.Up = 0.0f;
        break;
    case KeyCode::ShiftKey:
        m_CameraController.Shift = false;
        break;
    }
}

void RaytracingDemo::OnMouseMoved(MouseMotionEventArgs& e)
{
    if (m_ImGui != nullptr && m_ImGui->WantsToCaptureMouse())
    {
        return;
    }

    Base::OnMouseMoved(e);

    if (e.LeftButton)
    {
        if (e.RelX != 0 || e.RelY != 0)
        {
            m_CameraController.Pitch = Clamp(m_CameraController.Pitch - e.RelY * m_MouseRotateSpeed, -90.0f, 90.0f);
            m_CameraController.Yaw -= e.RelX * m_MouseRotateSpeed;
            ResetAccumulation();
        }
        return;
    }

    if (e.MiddleButton)
    {
        if (e.RelX != 0 || e.RelY != 0)
        {
            const XMVECTOR cameraPan = XMVectorSet(
                static_cast<float>(-e.RelX) * m_MousePanSpeed,
                static_cast<float>(e.RelY) * m_MousePanSpeed,
                0.0f,
                0.0f);
            m_Camera.Translate(cameraPan, Space::Local);
            ResetAccumulation();
        }
        return;
    }

    if (e.RightButton)
    {
        if (e.RelX != 0)
        {
            const XMVECTOR cameraForward = XMVectorSet(
                0.0f,
                0.0f,
                static_cast<float>(e.RelX) * m_MouseDollySpeed,
                0.0f);
            m_Camera.Translate(cameraForward, Space::Local);
            ResetAccumulation();
        }
    }
}

void RaytracingDemo::OnMouseWheel(MouseWheelEventArgs& e)
{
    if (m_ImGui != nullptr && m_ImGui->WantsToCaptureMouse())
    {
        return;
    }

    Base::OnMouseWheel(e);
    if (e.WheelDelta != 0.0f)
    {
        const XMVECTOR cameraForward = XMVectorSet(
            0.0f,
            0.0f,
            e.WheelDelta * m_MouseWheelDollySpeed,
            0.0f);
        m_Camera.Translate(cameraForward, Space::Local);
        ResetAccumulation();
    }
}

void RaytracingDemo::OnResize(ResizeEventArgs& e)
{
    Base::OnResize(e);

    if (m_Width == e.Width && m_Height == e.Height)
    {
        return;
    }

    m_Width = std::max(1, e.Width);
    m_Height = std::max(1, e.Height);
    m_FrameIndex = 0;
    ResetAccumulation();

    const float aspectRatio = static_cast<float>(m_Width) / static_cast<float>(m_Height);
    m_Camera.SetProjection(m_CameraFov, aspectRatio, 0.1f, 1000.0f);

    if (m_RenderGraph != nullptr)
    {
        m_RenderGraph->MarkDirty();
    }
}
