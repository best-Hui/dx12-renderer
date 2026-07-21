#include <RaytracingDemo.h>

#include <DX12Library/Application.h>
#include <DX12Library/CommandList.h>
#include <DX12Library/CommandQueue.h>
#include <DX12Library/Events.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/Window.h>

#include <DXR/RaytracingPipeline.h>

#include <Framework/GraphicsSettings.h>
#include <Framework/Mesh.h>
#include <Framework/ModelLoader.h>
#include <Framework/ShaderBlob.h>

#include <DirectXMath.h>

#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace
{
    template<typename T>
    constexpr const T& Clamp(const T& val, const T& min, const T& max)
    {
        return val < min ? min : val > max ? max : val;
    }

    XMFLOAT4 NormalizeColor(const XMFLOAT4& value)
    {
        XMVECTOR vector = XMLoadFloat4(&value);
        vector = XMVector3Normalize(vector);
        XMFLOAT4 result{};
        XMStoreFloat4(&result, vector);
        return result;
    }

    float GetColorMagnitude(const XMFLOAT4& value)
    {
        XMVECTOR vector = XMLoadFloat4(&value);
        float result = 0.0f;
        XMStoreFloat(&result, XMVector3Length(vector));
        return result;
    }
}

RaytracingDemo::RaytracingDemo(const std::wstring& name, const int width, const int height, GraphicsSettings graphicsSettings)
    : Base(name, width, height, false)
    , m_Width(width)
    , m_Height(height)
{
    (void)graphicsSettings;

    const XMVECTOR cameraPos = XMVectorSet(0, 5, -20, 1);
    const XMVECTOR cameraTarget = XMVectorSet(0, 5, 0, 1);
    const XMVECTOR cameraUp = XMVectorSet(0, 1, 0, 0);
    m_Camera.SetLookAt(cameraPos, cameraTarget, cameraUp);
    m_Camera.SetProjection(45.0f, static_cast<float>(m_Width) / static_cast<float>(m_Height), 0.1f, 1000.0f);
}

bool RaytracingDemo::LoadContent()
{
    Assert(Dxr::RaytracingPipeline::IsSupported(), "DirectX Raytracing is not supported by the selected adapter.");

    const auto commandQueue = Application::Get().GetCommandQueue();
    const auto commandList = commandQueue->GetCommandList();

    m_RaytracingRenderer = std::make_unique<Dxr::RaytracingRenderer>(ShaderBlob(L"RaytracingDemo_RT.cso"));
    m_RaytracingRenderer->Resize(m_Width, m_Height);

    LoadDeferredLightingScene(*commandList);

    const std::vector<Dxr::MeshInstance> instances = CreateRaytracingInstances();
    m_RaytracingScene.Build(*commandList, instances);
    m_RaytracingRenderer->SetMaterials(*commandList, m_Materials, m_Textures);
    m_RaytracingRenderer->SetScene(*commandList, m_RaytracingScene);

    const uint64_t fenceValue = commandQueue->ExecuteCommandList(commandList);
    commandQueue->WaitForFenceValue(fenceValue);
    return true;
}

void RaytracingDemo::UnloadContent()
{
    m_RaytracingRenderer.reset();
    m_SceneObjects.clear();
    m_Materials.clear();
    m_Textures.clear();
}

uint32_t RaytracingDemo::AddTexture(CommandList& commandList, const std::wstring& path, TextureUsageType usage)
{
    auto texture = std::make_shared<Texture>();
    commandList.LoadTextureFromFile(*texture, path, usage);
    m_Textures.push_back(texture);
    return static_cast<uint32_t>(m_Textures.size() - 1);
}

uint32_t RaytracingDemo::AddMaterial(const Dxr::MaterialData& material)
{
    m_Materials.push_back(material);
    return static_cast<uint32_t>(m_Materials.size() - 1);
}

uint32_t RaytracingDemo::AddDiffuseMaterial(
    const XMFLOAT4& diffuse,
    const XMFLOAT4& tilingOffset,
    const uint32_t diffuseTextureIndex)
{
    Dxr::MaterialData material{};
    material.Diffuse = diffuse;
    material.TilingOffset = tilingOffset;
    material.DiffuseTextureIndex = diffuseTextureIndex;
    return AddMaterial(material);
}

void RaytracingDemo::LoadDeferredLightingScene(CommandList& commandList)
{
    ModelLoader modelLoader;

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

    for (const PointLight& pointLight : m_PointLights)
    {
        const uint32_t material = AddDiffuseMaterial(NormalizeColor(pointLight.Color), { 1, 1, 0, 0 }, whiteTexture);
        auto model = modelLoader.LoadExisting(Mesh::CreateSphere(commandList, 0.5f));
        XMVECTOR position = XMLoadFloat4(&pointLight.PositionWs);
        XMMATRIX worldMatrix = XMMatrixTranslationFromVector(position);
        m_SceneObjects.push_back({ worldMatrix, model, material });
    }
}

std::vector<Dxr::MeshInstance> RaytracingDemo::CreateRaytracingInstances() const
{
    std::vector<Dxr::MeshInstance> instances;

    for (const SceneObject& object : m_SceneObjects)
    {
        for (const auto& mesh : object.Model->GetMeshes())
        {
            instances.push_back({
                mesh,
                object.WorldMatrix,
                object.MaterialIndex
            });
        }
    }

    return instances;
}

void RaytracingDemo::OnUpdate(UpdateEventArgs& e)
{
    Base::OnUpdate(e);
    m_DeltaTime = static_cast<float>(e.ElapsedTime);

    const float baseSpeed = m_CameraController.Shift ? 25.0f : 10.0f;
    const float speed = baseSpeed * m_DeltaTime;

    XMVECTOR translation = m_Camera.GetTranslation();
    const XMVECTOR forward = m_Camera.GetForward();
    const XMVECTOR right = m_Camera.GetRight();
    const XMVECTOR up = m_Camera.GetUp();

    translation += forward * speed * (m_CameraController.Forward - m_CameraController.Backward);
    translation += right * speed * (m_CameraController.Right - m_CameraController.Left);
    translation += up * speed * (m_CameraController.Up - m_CameraController.Down);
    m_Camera.SetTranslation(translation);
}

void RaytracingDemo::OnRender(RenderEventArgs& e)
{
    Base::OnRender(e);

    const auto commandQueue = Application::Get().GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
    const auto commandList = commandQueue->GetCommandList();

    Dxr::CameraConstants camera{};
    camera.InverseView = XMMatrixInverse(nullptr, m_Camera.GetViewMatrix());
    camera.InverseProjection = XMMatrixInverse(nullptr, m_Camera.GetProjectionMatrix());
    XMStoreFloat4(&camera.CameraPosition, m_Camera.GetTranslation());
    camera.Width = static_cast<uint32_t>(m_Width);
    camera.Height = static_cast<uint32_t>(m_Height);

    {
        PIXScope(*commandList, "Raytracing Demo");
        m_RaytracingRenderer->Render(*commandList, m_RaytracingScene, camera);
    }

    commandQueue->ExecuteCommandList(commandList);
    PWindow->Present(*m_RaytracingRenderer->GetOutputTexture());
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

    const XMVECTOR rotation = XMQuaternionRotationRollPitchYaw(
        XMConvertToRadians(m_CameraController.Pitch),
        XMConvertToRadians(m_CameraController.Yaw),
        0.0f);
    m_Camera.SetRotation(rotation);
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

    if (m_RaytracingRenderer != nullptr)
    {
        m_RaytracingRenderer->Resize(m_Width, m_Height);
    }
}
