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
#include <Framework/ShaderBlob.h>

#include <DirectXMath.h>
#include <d3dx12.h>

#include <algorithm>
#include <cmath>
#include <vector>

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
    m_Camera.SetProjection(45.0f, static_cast<float>(m_Width) / static_cast<float>(m_Height), 0.1f, 1000.0f);
}

bool RaytracingDemo::LoadContent()
{
    Assert(RayTracingShader::IsSupported(), "DirectX Raytracing is not supported by the selected adapter.");

    const auto commandQueue = Application::Get().GetCommandQueue();
    const auto commandList = commandQueue->GetCommandList();

    m_RayTracingShader = std::make_unique<RayTracingShader>(ShaderBlob(L"RaytracingDemo_RT.cso"));
    ResizeRayTracingOutputTexture();

    LoadDeferredLightingScene(*commandList);

    Assert(!m_Textures.empty(), "RaytracingDemo needs at least one texture before creating the common root signature.");
    m_RootSignature = std::make_shared<CommonRootSignature>(m_Textures.front());
    m_Taa = std::make_unique<TAA>(m_RootSignature, *commandList, Window::BUFFER_FORMAT, m_Width, m_Height);

    const auto velocityDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_R16G16_FLOAT,
        m_Width,
        m_Height,
        1,
        1,
        1,
        0,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    m_TaaVelocityTexture = std::make_shared<Texture>(velocityDesc, nullptr, TextureUsageType::RenderTarget, L"Ray Tracing TAA Zero Velocity");

    AddRaytracingInstances();

    RayTracingAccelerationStructureBuildSettings accelerationStructureSettings{};
    accelerationStructureSettings.AllowUpdate = true;
    m_RayTracingAccelerationStructure.Build(*commandList, accelerationStructureSettings);
    commandList->CopyStructuredBuffer(m_MaterialBuffer, m_Materials);
    commandList->CopyStructuredBuffer(m_GeometryBuffer, m_RayTracingAccelerationStructure.GetGeometryData());

    m_RayTracingShader->SetOutputTexture("Output", m_RayTracingOutputTexture);
    m_RayTracingShader->SetAccelerationStructure("Scene", m_RayTracingAccelerationStructure);
    m_RayTracingShader->SetStructuredBuffer("Materials", m_MaterialBuffer);
    m_RayTracingShader->SetStructuredBuffer("Geometries", m_GeometryBuffer);
    m_RayTracingShader->SetTextureArray("Textures", m_Textures);

    const uint64_t fenceValue = commandQueue->ExecuteCommandList(commandList);
    commandQueue->WaitForFenceValue(fenceValue);
    return true;
}

void RaytracingDemo::UnloadContent()
{
    m_TaaVelocityTexture.reset();
    m_Taa.reset();
    m_RootSignature.reset();
    m_RayTracingOutputTexture.reset();
    m_RayTracingShader.reset();
    m_SceneObjects.clear();
    m_Materials.clear();
    m_Textures.clear();
}

void RaytracingDemo::ResizeRayTracingOutputTexture()
{
    const auto width = static_cast<uint32_t>(std::max(1, m_Width));
    const auto height = static_cast<uint32_t>(std::max(1, m_Height));

    if (m_RayTracingOutputTexture == nullptr)
    {
        const auto outputDesc = CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_R8G8B8A8_UNORM,
            width,
            height,
            1,
            1,
            1,
            0,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

        m_RayTracingOutputTexture = std::make_shared<Texture>(outputDesc, nullptr, TextureUsageType::Other, L"Ray Tracing Output");
    }
    else
    {
        m_RayTracingOutputTexture->Resize(width, height);
    }

    if (m_RayTracingShader != nullptr)
    {
        m_RayTracingShader->SetOutputTexture("Output", m_RayTracingOutputTexture);
    }
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
    const uint32_t diffuseTextureIndex)
{
    MaterialData material{};
    material.Diffuse = diffuse;
    material.TilingOffset = tilingOffset;
    material.DiffuseTextureIndex = diffuseTextureIndex;
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

    const uint32_t groundMaterial = AddDiffuseMaterial({ 1, 1, 1, 1 }, { 6, 6, 0, 0 }, groundTexture);
    const uint32_t chestMaterial = AddDiffuseMaterial({ 1, 1, 1, 1 }, { 1, 1, 0, 0 }, chestTexture);
    const uint32_t mirrorMaterial = AddDiffuseMaterial({ 0.85f, 0.85f, 0.92f, 1 }, { 1, 1, 0, 0 }, whiteTexture);
    const uint32_t cubeMaterial = AddDiffuseMaterial({ 0.9f, 0.9f, 0.9f, 1 }, { 1, 1, 0, 0 }, whiteTexture);
    const uint32_t cerberusMaterial = AddDiffuseMaterial({ 1, 1, 1, 1 }, { 1, 1, 0, 0 }, cerberusTexture);
    const uint32_t tvMaterial = AddDiffuseMaterial({ 1, 1, 1, 1 }, { 1, 1, 0, 0 }, tvTexture);

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

            const uint32_t material = AddDiffuseMaterial(color, { 1, 1, 0, 0 }, whiteTexture);
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

    const auto commandQueue = Application::Get().GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
    const auto commandList = commandQueue->GetCommandList();

    CameraConstants camera{};
    camera.InverseView = XMMatrixInverse(nullptr, m_Camera.GetViewMatrix());
    camera.InverseProjection = XMMatrixInverse(nullptr, m_Camera.GetProjectionMatrix());
    XMStoreFloat4(&camera.CameraPosition, m_Camera.GetTranslation());
    camera.Width = static_cast<uint32_t>(m_Width);
    camera.Height = static_cast<uint32_t>(m_Height);
    camera.MaxBounces = 5;
    camera.SamplesPerPixel = 1;
    camera.LightCount = static_cast<uint32_t>(std::min<size_t>(m_PointLights.size(), MaxPathTracingLights));
    camera.FrameIndex = m_FrameIndex++;
    camera.TaaJitterOffset = m_Taa->ComputeJitterOffset();

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

    {
        PIXScope(*commandList, "Raytracing Demo");
        m_RayTracingShader->SetConstantBufferData("CameraConstants", &camera, sizeof(camera));
        m_RayTracingShader->Dispatch(*commandList, "RayGen", static_cast<uint32_t>(m_Width), static_cast<uint32_t>(m_Height));
    }

    {
        static constexpr float ZERO_VELOCITY[] = { 0.0f, 0.0f, 0.0f, 0.0f };
        commandList->ClearTexture(*m_TaaVelocityTexture, ZERO_VELOCITY);

        m_RootSignature->Bind(*commandList);
        m_Taa->Resolve(*commandList, m_RayTracingOutputTexture, m_TaaVelocityTexture, 0.88f);
        m_Taa->OnRenderedFrame(m_Camera.GetViewMatrix() * m_Camera.GetProjectionMatrix());
    }

    commandQueue->ExecuteCommandList(commandList);
    PWindow->Present(*m_Taa->GetResolvedTexture());
}

void RaytracingDemo::OnKeyPressed(KeyEventArgs& e)
{
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
    Base::OnMouseMoved(e);

    if (!e.LeftButton)
    {
        return;
    }

    constexpr float mouseSpeed = 0.1f;
    m_CameraController.Pitch = Clamp(m_CameraController.Pitch - e.RelY * mouseSpeed, -90.0f, 90.0f);
    m_CameraController.Yaw -= e.RelX * mouseSpeed;

}

void RaytracingDemo::OnMouseWheel(MouseWheelEventArgs& e)
{
    Base::OnMouseWheel(e);
    float fov = m_Camera.GetFov();
    fov = Clamp(fov - e.WheelDelta, 12.0f, 90.0f);
    m_Camera.SetFov(fov);
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

    const float aspectRatio = static_cast<float>(m_Width) / static_cast<float>(m_Height);
    m_Camera.SetProjection(45.0f, aspectRatio, 0.1f, 1000.0f);

    if (m_RayTracingOutputTexture != nullptr)
    {
        ResizeRayTracingOutputTexture();
    }

    if (m_Taa != nullptr)
    {
        m_Taa->Resize(m_Width, m_Height);
    }

    if (m_TaaVelocityTexture != nullptr)
    {
        m_TaaVelocityTexture->Resize(m_Width, m_Height);
    }
}
