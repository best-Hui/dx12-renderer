#include <RaytracingDemo.h>

#include <Passes/NrdPass.h>
#include <Passes/SvgfPass.h>

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
#include <random>
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
    , m_DirectionalLightBuffer(L"Ray Tracing Directional Lights")
    , m_PointLightBuffer(L"Ray Tracing Point Lights")
    , m_AreaLightBuffer(L"Ray Tracing Area Lights")
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
    if (mode != nullptr && std::strcmp(mode, "shader-table") == 0)
    {
        m_PathTracingBackend = PathTracingBackend::ShaderTableDxr;
    }
    std::free(mode);

    char* nrdMode = nullptr;
    size_t nrdModeLength = 0;
    _dupenv_s(&nrdMode, &nrdModeLength, "RAYTRACING_DEMO_NRD");
    if (nrdMode != nullptr)
    {
        m_DenoiserAlgorithm = std::strcmp(nrdMode, "0") != 0 ? DenoiserAlgorithm::Nrd : DenoiserAlgorithm::Off;
    }
    std::free(nrdMode);

    char* denoiserMode = nullptr;
    size_t denoiserModeLength = 0;
    _dupenv_s(&denoiserMode, &denoiserModeLength, "RAYTRACING_DEMO_DENOISER");
    if (denoiserMode != nullptr)
    {
        if (std::strcmp(denoiserMode, "nrd") == 0)
        {
            m_DenoiserAlgorithm = DenoiserAlgorithm::Nrd;
        }
        else if (std::strcmp(denoiserMode, "svgf") == 0)
        {
            m_DenoiserAlgorithm = DenoiserAlgorithm::Svgf;
        }
        else
        {
            m_DenoiserAlgorithm = DenoiserAlgorithm::Off;
        }
    }
    std::free(denoiserMode);
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
        ShaderBlob(L"GBuffer.vs.cso"),
        ShaderBlob(L"GBuffer.ps.cso"),
        [](PipelineStateBuilder&) {},
        false);

    m_SkyboxShader = std::make_shared<Shader>(
        m_RootSignature,
        ShaderBlob(L"Skybox.vs.cso"),
        ShaderBlob(L"Skybox.ps.cso"),
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
    rayTracingDesc.Bindings.push_back({ "GBufferTextures", RayTracingShaderBindingType::TextureArray, 0, 4, 5 });
    rayTracingDesc.Bindings.push_back({ "Skybox", RayTracingShaderBindingType::TextureArray, 0, 5, 1 });
    rayTracingDesc.Bindings.push_back({ "DepthTexture", RayTracingShaderBindingType::TextureArray, 0, 6, 1 });
    rayTracingDesc.Bindings.push_back({ "DirectionalLights", RayTracingShaderBindingType::StructuredBuffer, 3, 0, 1 });
    rayTracingDesc.Bindings.push_back({ "PointLights", RayTracingShaderBindingType::StructuredBuffer, 4, 0, 1 });
    rayTracingDesc.Bindings.push_back({ "AreaLights", RayTracingShaderBindingType::StructuredBuffer, 5, 0, 1 });
    rayTracingDesc.Bindings.push_back({ "Accumulation", RayTracingShaderBindingType::OutputTexture, 1, 0, 1 });
    rayTracingDesc.Bindings.push_back({ "NrdNoisyRadiance", RayTracingShaderBindingType::OutputTexture, 2, 0, 1 });
    rayTracingDesc.PayloadSizeInBytes = 64;
    m_RayTracingShader = std::make_unique<RayTracingShader>(ShaderBlob(L"PathTracing.rt.cso"), rayTracingDesc);
    m_InlinePathTracingShader = std::make_unique<ComputeShader>(m_RootSignature, ShaderBlob(L"InlinePathTracing.cs.cso"));
    m_NrdPass = std::make_unique<NrdPass>(m_RootSignature);
    m_SvgfPass = std::make_unique<SvgfPass>(m_RootSignature);
    char* nrdDenoiserMode = nullptr;
    size_t nrdDenoiserModeLength = 0;
    _dupenv_s(&nrdDenoiserMode, &nrdDenoiserModeLength, "RAYTRACING_DEMO_NRD_MODE");
    if (nrdDenoiserMode != nullptr && std::strcmp(nrdDenoiserMode, "reblur") == 0)
    {
        m_NrdPass->GetSettings().Mode = NrdPass::DenoiserMode::ReblurDiffuse;
    }
    std::free(nrdDenoiserMode);
    ApplyDenoiserSelection();
    if (IsDenoiserEnabled())
    {
        m_AccumulationEnabled = false;
    }

    AddRaytracingInstances();

    RayTracingAccelerationStructureBuildSettings accelerationStructureSettings{};
    accelerationStructureSettings.AllowUpdate = true;
    m_RayTracingAccelerationStructure.Build(*commandList, accelerationStructureSettings);
    commandList->CopyStructuredBuffer(m_MaterialBuffer, m_Materials);
    commandList->CopyStructuredBuffer(m_GeometryBuffer, m_RayTracingAccelerationStructure.GetGeometryData());
    InitializeSceneLightBuffers(*commandList);
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
    m_NrdPass.reset();
    m_RootSignature.reset();
    m_SkyboxTexture.reset();
    m_InlinePathTracingShader.reset();
    m_RayTracingShader.reset();
    m_SceneObjects.clear();
    m_Materials.clear();
    m_Textures.clear();
    m_LightUploadBuffer.reset();
}

void RaytracingDemo::BindRayTracingShaderResources()
{
    m_RayTracingShader->SetAccelerationStructure("Scene", m_RayTracingAccelerationStructure);
    m_RayTracingShader->SetStructuredBuffer("Materials", m_MaterialBuffer);
    m_RayTracingShader->SetStructuredBuffer("Geometries", m_GeometryBuffer);
    m_RayTracingShader->SetStructuredBuffer("DirectionalLights", m_DirectionalLightBuffer);
    m_RayTracingShader->SetStructuredBuffer("PointLights", m_PointLightBuffer);
    m_RayTracingShader->SetStructuredBuffer("AreaLights", m_AreaLightBuffer);
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
    camera.DirectionalLightCount = static_cast<uint32_t>(std::min<size_t>(m_DirectionalLights.size(), MaxDirectionalLights));
    camera.PointLightCount = static_cast<uint32_t>(std::min<size_t>(m_PointLights.size(), MaxPointLights));
    camera.AreaLightCount = static_cast<uint32_t>(std::min<size_t>(m_AreaLights.size(), MaxAreaLights));
    camera.FrameIndex = m_FrameIndex;
    const bool pathAccumulationEnabled = m_AccumulationEnabled && !IsDenoiserEnabled();
    camera.AccumulationFrameIndex = pathAccumulationEnabled ? m_AccumulationFrameIndex : 0u;
    camera.AccumulationEnabled = pathAccumulationEnabled ? 1u : 0u;
    if (m_NrdPass != nullptr)
    {
        const NrdPass::Settings& nrdSettings = m_NrdPass->GetSettings();
        camera.NrdDenoiserMode = static_cast<uint32_t>(nrdSettings.Mode);
        camera.NrdReblurHitDistanceScale = nrdSettings.ReblurHitDistanceScale;
    }
    camera.SkyLight = m_SkyLight;
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
    if (m_NrdPass != nullptr)
    {
        m_NrdPass->ResetHistory();
    }
    if (m_SvgfPass != nullptr)
    {
        m_SvgfPass->ResetHistory();
    }
}

void RaytracingDemo::ApplyDenoiserSelection()
{
    if (m_NrdPass != nullptr)
    {
        m_NrdPass->SetEnabled(IsNrdDenoiserEnabled());
    }
    if (m_SvgfPass != nullptr)
    {
        m_SvgfPass->SetEnabled(IsSvgfDenoiserEnabled());
    }
}

void RaytracingDemo::OnImGui()
{
    ImGui::SetNextWindowSize(ImVec2(520.0f, 680.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Raytracing");
    ImGui::Text("GBuffer Path Tracing");
    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
    ImGui::Text("Resolution: %d x %d", m_Width, m_Height);
    ImGui::Text("Frame: %u", m_FrameIndex);
    ImGui::Text("Accumulation: %u", m_AccumulationFrameIndex);

    if (ImGui::Checkbox("Enable Accumulation", &m_AccumulationEnabled))
    {
        ResetAccumulation();
    }

    const char* denoiserNames[] = { "Off", "NRD", "SVGF" };
    int selectedDenoiser = static_cast<int>(m_DenoiserAlgorithm);
    if (ImGui::Combo("Denoiser", &selectedDenoiser, denoiserNames, 3))
    {
        m_DenoiserAlgorithm = static_cast<DenoiserAlgorithm>(selectedDenoiser);
        ApplyDenoiserSelection();
        if (IsDenoiserEnabled())
        {
            m_AccumulationEnabled = false;
        }
        ResetAccumulation();
    }
    if (m_NrdPass != nullptr && !m_NrdPass->IsAvailable())
    {
        ImGui::Text("NRD unavailable");
    }
    if (IsNrdDenoiserEnabled() && m_NrdPass != nullptr && ImGui::CollapsingHeader("NRD Settings", ImGuiTreeNodeFlags_DefaultOpen))
    {
        NrdPass::Settings& nrdSettings = m_NrdPass->GetSettings();
        bool nrdChanged = false;

        const char* denoiserNames[] = { "RELAX Diffuse", "ReBLUR Diffuse" };
        int denoiserMode = static_cast<int>(nrdSettings.Mode);
        if (ImGui::Combo("Denoiser", &denoiserMode, denoiserNames, 2))
        {
            nrdSettings.Mode = static_cast<NrdPass::DenoiserMode>(denoiserMode);
            nrdChanged = true;
        }

        nrdChanged |= ImGui::SliderFloat("Denoising Range", &nrdSettings.DenoisingRange, 1.0f, 500000.0f, "%.0f");

        auto sliderUint = [&nrdChanged](const char* label, uint32_t& value, int minValue, int maxValue)
        {
            int temporaryValue = static_cast<int>(value);
            if (ImGui::SliderInt(label, &temporaryValue, minValue, maxValue))
            {
                value = static_cast<uint32_t>(temporaryValue);
                nrdChanged = true;
            }
        };

        if (nrdSettings.Mode == NrdPass::DenoiserMode::RelaxDiffuse)
        {
            sliderUint("Relax History", nrdSettings.RelaxDiffuseMaxAccumulatedFrameNum, 0, 255);
            sliderUint("Relax Fast History", nrdSettings.RelaxDiffuseMaxFastAccumulatedFrameNum, 0, 255);
            sliderUint("Relax History Fix", nrdSettings.RelaxHistoryFixFrameNum, 0, 3);
            sliderUint("Relax History Stride", nrdSettings.RelaxHistoryFixBasePixelStride, 1, 32);
            nrdChanged |= ImGui::SliderFloat("Relax History Sigma", &nrdSettings.RelaxFastHistoryClampingSigmaScale, 1.0f, 3.0f, "%.2f");
            nrdChanged |= ImGui::SliderFloat("Relax Prepass Radius", &nrdSettings.RelaxDiffusePrepassBlurRadius, 0.0f, 80.0f, "%.1f");
            nrdChanged |= ImGui::SliderFloat("Relax Min Hit Weight", &nrdSettings.RelaxMinHitDistanceWeight, 0.001f, 0.2f, "%.3f");
            sliderUint("Relax Variance History", nrdSettings.RelaxSpatialVarianceEstimationHistoryThreshold, 0, 16);
            nrdChanged |= ImGui::SliderFloat("Relax Phi Luminance", &nrdSettings.RelaxDiffusePhiLuminance, 0.1f, 16.0f, "%.2f");
            nrdChanged |= ImGui::SliderFloat("Relax Lobe Fraction", &nrdSettings.RelaxLobeAngleFraction, 0.01f, 1.0f, "%.2f");
            nrdChanged |= ImGui::SliderFloat("Relax Roughness Fraction", &nrdSettings.RelaxRoughnessFraction, 0.01f, 1.0f, "%.2f");
            sliderUint("Relax A-Trous Iterations", nrdSettings.RelaxAtrousIterationNum, 2, 8);
            nrdChanged |= ImGui::SliderFloat("Relax Depth Threshold", &nrdSettings.RelaxDepthThreshold, 0.0001f, 0.05f, "%.4f");
            nrdChanged |= ImGui::SliderFloat("Relax Luma Relax", &nrdSettings.RelaxLuminanceEdgeStoppingRelaxation, 0.0f, 2.0f, "%.2f");
            nrdChanged |= ImGui::SliderFloat("Relax Normal Relax", &nrdSettings.RelaxNormalEdgeStoppingRelaxation, 0.0f, 2.0f, "%.2f");
            nrdChanged |= ImGui::SliderFloat("Relax Roughness Relax", &nrdSettings.RelaxRoughnessEdgeStoppingRelaxation, 0.0f, 2.0f, "%.2f");
            nrdChanged |= ImGui::Checkbox("Relax Anti-Firefly", &nrdSettings.RelaxEnableAntiFirefly);
            nrdChanged |= ImGui::Checkbox("Relax Roughness Stop", &nrdSettings.RelaxEnableRoughnessEdgeStopping);
        }
        else
        {
            nrdChanged |= ImGui::SliderFloat("ReBLUR Hit Scale", &nrdSettings.ReblurHitDistanceScale, 1.0f, 1000.0f, "%.1f");
            sliderUint("ReBLUR History", nrdSettings.ReblurMaxAccumulatedFrameNum, 0, 63);
            sliderUint("ReBLUR Fast History", nrdSettings.ReblurMaxFastAccumulatedFrameNum, 0, 63);
            sliderUint("ReBLUR History Fix", nrdSettings.ReblurHistoryFixFrameNum, 0, 16);
            sliderUint("ReBLUR History Stride", nrdSettings.ReblurHistoryFixBasePixelStride, 1, 32);
            nrdChanged |= ImGui::SliderFloat("ReBLUR History Sigma", &nrdSettings.ReblurFastHistoryClampingSigmaScale, 1.0f, 3.0f, "%.2f");
            nrdChanged |= ImGui::SliderFloat("ReBLUR Prepass Radius", &nrdSettings.ReblurDiffusePrepassBlurRadius, 0.0f, 80.0f, "%.1f");
            nrdChanged |= ImGui::SliderFloat("ReBLUR Min Hit Weight", &nrdSettings.ReblurMinHitDistanceWeight, 0.001f, 0.2f, "%.3f");
            nrdChanged |= ImGui::SliderFloat("ReBLUR Min Radius", &nrdSettings.ReblurMinBlurRadius, 0.0f, 16.0f, "%.1f");
            nrdChanged |= ImGui::SliderFloat("ReBLUR Max Radius", &nrdSettings.ReblurMaxBlurRadius, 0.0f, 80.0f, "%.1f");
            nrdChanged |= ImGui::SliderFloat("ReBLUR Lobe Fraction", &nrdSettings.ReblurLobeAngleFraction, 0.01f, 1.0f, "%.2f");
            nrdChanged |= ImGui::SliderFloat("ReBLUR Roughness Fraction", &nrdSettings.ReblurRoughnessFraction, 0.01f, 1.0f, "%.2f");
            nrdChanged |= ImGui::SliderFloat("ReBLUR Plane Sensitivity", &nrdSettings.ReblurPlaneDistanceSensitivity, 0.001f, 0.2f, "%.3f");
            nrdChanged |= ImGui::SliderFloat("ReBLUR Firefly Scale", &nrdSettings.ReblurFireflySuppressorMinRelativeScale, 1.0f, 3.0f, "%.2f");
            nrdChanged |= ImGui::Checkbox("ReBLUR Anti-Firefly", &nrdSettings.ReblurEnableAntiFirefly);
        }

        if (nrdChanged)
        {
            m_NrdPass->ResetHistory();
            ResetAccumulation();
        }
    }
    if (IsSvgfDenoiserEnabled() && m_SvgfPass != nullptr && ImGui::CollapsingHeader("SVGF Settings", ImGuiTreeNodeFlags_DefaultOpen))
    {
        SvgfPass::Settings& svgfSettings = m_SvgfPass->GetSettings();
        bool svgfChanged = false;
        int atrousIterations = static_cast<int>(svgfSettings.AtrousIterations);
        if (ImGui::SliderInt("SVGF A-Trous Iterations", &atrousIterations, 1, 8))
        {
            svgfSettings.AtrousIterations = static_cast<uint32_t>(atrousIterations);
            svgfChanged = true;
        }
        svgfChanged |= ImGui::SliderFloat("SVGF Temporal Alpha", &svgfSettings.TemporalAlpha, 0.001f, 1.0f, "%.3f");
        svgfChanged |= ImGui::SliderFloat("SVGF Moments Alpha", &svgfSettings.MomentsAlpha, 0.001f, 1.0f, "%.3f");
        svgfChanged |= ImGui::SliderFloat("SVGF Phi Color", &svgfSettings.PhiColor, 0.1f, 32.0f, "%.2f");
        svgfChanged |= ImGui::SliderFloat("SVGF Phi Normal", &svgfSettings.PhiNormal, 1.0f, 256.0f, "%.1f");
        svgfChanged |= ImGui::SliderFloat("SVGF Phi Depth", &svgfSettings.PhiDepth, 0.001f, 10.0f, "%.3f");
        if (svgfChanged)
        {
            m_SvgfPass->ResetHistory();
            ResetAccumulation();
        }
    }
    if (ImGui::Checkbox("Animate Point Lights", &m_AnimatePointLights))
    {
        ResetAccumulation();
    }
    ImGui::Text("Point Lights: %zu", m_PointLights.size());
    if (!m_PointLights.empty())
    {
        ImGui::Text("PointLight[0].Y: %.2f", m_PointLights.front().PositionWs.y);
    }

    const char* modeNames[] = { "Inline Ray Query", "Shader Table DXR" };
    int selectedMode = static_cast<int>(m_PathTracingBackend);
    if (ImGui::Combo("Mode", &selectedMode, modeNames, 2))
    {
        m_PathTracingBackend = static_cast<PathTracingBackend>(selectedMode);
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

uint32_t RaytracingDemo::AddPbrMaterial(
    const XMFLOAT4& diffuse,
    const XMFLOAT4& tilingOffset,
    const uint32_t diffuseTextureIndex,
    const uint32_t normalTextureIndex,
    const uint32_t metallicTextureIndex,
    const uint32_t roughnessTextureIndex,
    const uint32_t ambientOcclusionTextureIndex,
    const float metallic,
    const float roughness,
    const bool hasDiffuseMap,
    const bool hasNormalMap,
    const bool hasMetallicMap,
    const bool hasRoughnessMap,
    const bool hasAmbientOcclusionMap)
{
    MaterialData material{};
    material.Diffuse = diffuse;
    material.Specular = { 0.04f, 0.04f, 0.04f, 1.0f };
    material.TilingOffset = tilingOffset;
    material.DiffuseTextureIndex = diffuseTextureIndex;
    material.NormalTextureIndex = normalTextureIndex;
    material.MetallicTextureIndex = metallicTextureIndex;
    material.RoughnessTextureIndex = roughnessTextureIndex;
    material.AmbientOcclusionTextureIndex = ambientOcclusionTextureIndex;
    material.HasDiffuseMap = hasDiffuseMap ? 1u : 0u;
    material.HasNormalMap = hasNormalMap ? 1u : 0u;
    material.HasMetallicMap = hasMetallicMap ? 1u : 0u;
    material.HasRoughnessMap = hasRoughnessMap ? 1u : 0u;
    material.HasAmbientOcclusionMap = hasAmbientOcclusionMap ? 1u : 0u;
    material.Metallic = metallic;
    material.Roughness = roughness;
    return AddMaterial(material);
}

uint32_t RaytracingDemo::AddDiffuseMaterial(
    const XMFLOAT4& diffuse,
    const XMFLOAT4& tilingOffset,
    const uint32_t diffuseTextureIndex,
    const float metallic,
    const float roughness)
{
    return AddPbrMaterial(
        diffuse,
        tilingOffset,
        diffuseTextureIndex,
        diffuseTextureIndex,
        diffuseTextureIndex,
        diffuseTextureIndex,
        diffuseTextureIndex,
        metallic,
        roughness,
        true);
}

void RaytracingDemo::LoadDeferredLightingScene(CommandList& commandList)
{
    ModelLoader modelLoader;
    m_DirectionalLights.clear();
    m_PointLights.clear();
    m_AreaLights.clear();
    m_PointLightBaseY.clear();
    m_PointLightPhase.clear();
    m_PointLightOrbitRadius.clear();
    m_PointLightOrbitSpeed.clear();

    const uint32_t whiteTexture = AddTexture(commandList, L"Assets/Textures/white.png");
    const uint32_t groundTexture = AddTexture(commandList, L"Assets/Textures/Ground047/Ground047_1K_Color.jpg");
    const uint32_t groundNormalTexture = AddTexture(commandList, L"Assets/Textures/Ground047/Ground047_1K_NormalDX.jpg", TextureUsageType::Normalmap);
    const uint32_t groundRoughnessTexture = AddTexture(commandList, L"Assets/Textures/Ground047/Ground047_1K_Roughness.jpg", TextureUsageType::Other);
    const uint32_t groundAoTexture = AddTexture(commandList, L"Assets/Textures/Ground047/Ground047_1K_AmbientOcclusion.jpg", TextureUsageType::Other);
    const uint32_t chestTexture = AddTexture(commandList, L"Assets/Models/old-wooden-chest/chest_01_BaseColor.png");
    const uint32_t chestNormalTexture = AddTexture(commandList, L"Assets/Models/old-wooden-chest/chest_01_Normal.png", TextureUsageType::Normalmap);
    const uint32_t chestMetallicTexture = AddTexture(commandList, L"Assets/Models/old-wooden-chest/chest_01_Metallic.png", TextureUsageType::Other);
    const uint32_t chestRoughnessTexture = AddTexture(commandList, L"Assets/Models/old-wooden-chest/chest_01_Roughness.png", TextureUsageType::Other);
    const uint32_t cerberusTexture = AddTexture(commandList, L"Assets/Models/cerberus/Cerberus_A.jpg");
    const uint32_t cerberusNormalTexture = AddTexture(commandList, L"Assets/Models/cerberus/Cerberus_N.jpg", TextureUsageType::Normalmap);
    const uint32_t cerberusMetallicTexture = AddTexture(commandList, L"Assets/Models/cerberus/Cerberus_M.jpg", TextureUsageType::Other);
    const uint32_t cerberusRoughnessTexture = AddTexture(commandList, L"Assets/Models/cerberus/Cerberus_R.jpg", TextureUsageType::Other);
    const uint32_t tvTexture = AddTexture(commandList, L"Assets/Models/tv/TV_Color.jpg");
    const uint32_t tvNormalTexture = AddTexture(commandList, L"Assets/Models/tv/TV_Normal.jpg", TextureUsageType::Normalmap);
    const uint32_t tvMetallicTexture = AddTexture(commandList, L"Assets/Models/tv/TV_Metallic.jpg", TextureUsageType::Other);
    const uint32_t tvRoughnessTexture = AddTexture(commandList, L"Assets/Models/tv/TV_Roughness.jpg", TextureUsageType::Other);
    const uint32_t tvAoTexture = AddTexture(commandList, L"Assets/Models/tv/TV_Occlusion.jpg", TextureUsageType::Other);

    const uint32_t groundMaterial = AddPbrMaterial({ 1, 1, 1, 1 }, { 6, 6, 0, 0 }, groundTexture, groundNormalTexture, whiteTexture, groundRoughnessTexture, groundAoTexture, 0.0f, 1.0f, true, true, false, true, true);
    const uint32_t chestMaterial = AddPbrMaterial({ 1, 1, 1, 1 }, { 1, 1, 0, 0 }, chestTexture, chestNormalTexture, chestMetallicTexture, chestRoughnessTexture, whiteTexture, 1.0f, 1.0f, true, true, true, true, false);
    const uint32_t mirrorMaterial = AddDiffuseMaterial({ 0.85f, 0.85f, 0.92f, 1 }, { 1, 1, 0, 0 }, whiteTexture, 1.0f, 0.08f);
    const uint32_t cubeMaterial = AddDiffuseMaterial({ 0.9f, 0.9f, 0.9f, 1 }, { 1, 1, 0, 0 }, whiteTexture, 0.0f, 0.35f);
    const uint32_t cerberusMaterial = AddPbrMaterial({ 1, 1, 1, 1 }, { 1, 1, 0, 0 }, cerberusTexture, cerberusNormalTexture, cerberusMetallicTexture, cerberusRoughnessTexture, whiteTexture, 1.0f, 1.0f, true, true, true, true, false);
    const uint32_t tvMaterial = AddPbrMaterial({ 1, 1, 1, 1 }, { 1, 1, 0, 0 }, tvTexture, tvNormalTexture, tvMetallicTexture, tvRoughnessTexture, tvAoTexture, 1.0f, 1.0f, true, true, true, true, true);

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

    CreateDemoLights();
}

void RaytracingDemo::CreateDemoLights()
{
    m_SkyLight.ColorAndIntensity = { 0.85f, 0.9f, 1.0f, 0.35f };

    DirectionalLight sunLight{};
    sunLight.m_DirectionWs = { -0.35f, 0.8f, -0.48f, 0.0f };
    sunLight.m_Color = { 1.0f, 0.95f, 0.82f, 0.8f };
    m_DirectionalLights.push_back(sunLight);

    constexpr uint32_t DemoPointLightCount = 5;
    const XMFLOAT3 orbitCenter = { -12.0f, 6.0f, 18.0f };
    const XMFLOAT4 lightColors[DemoPointLightCount] = {
        { 1.0f, 0.35f, 0.28f, 20.0f },
        { 0.35f, 0.75f, 1.0f, 18.0f },
        { 0.45f, 1.0f, 0.55f, 16.0f },
        { 1.0f, 0.85f, 0.35f, 18.0f },
        { 0.9f, 0.45f, 1.0f, 16.0f },
    };

    m_PointLights.reserve(DemoPointLightCount);
    m_PointLightBaseY.reserve(DemoPointLightCount);
    m_PointLightPhase.reserve(DemoPointLightCount);
    m_PointLightOrbitRadius.reserve(DemoPointLightCount);
    m_PointLightOrbitSpeed.reserve(DemoPointLightCount);

    for (uint32_t i = 0; i < DemoPointLightCount; ++i)
    {
        const float baseY = orbitCenter.y + static_cast<float>(i % 2) * 1.25f;
        const float phase = XM_2PI * static_cast<float>(i) / static_cast<float>(DemoPointLightCount);
        const float radius = 13.0f + static_cast<float>(i) * 2.0f;
        const float speed = 0.35f + static_cast<float>(i) * 0.07f;

        PointLight light(
            {
                orbitCenter.x + std::cos(phase) * radius,
                baseY,
                orbitCenter.z + std::sin(phase) * radius,
                1.0f
            },
            26.0f);
        light.Color = lightColors[i];
        light.RecalculateAttenuationCoefficients();

        m_PointLights.push_back(light);
        m_PointLightBaseY.push_back(baseY);
        m_PointLightPhase.push_back(phase);
        m_PointLightOrbitRadius.push_back(radius);
        m_PointLightOrbitSpeed.push_back(speed);
    }

    AreaLightData areaLight{};
    areaLight.PositionAndRange = { -18.0f, 10.0f, 18.0f, 35.0f };
    areaLight.NormalAndType = { 0.0f, -1.0f, 0.0f, 0.0f };
    areaLight.AxisUAndExtent = { 1.0f, 0.0f, 0.0f, 4.0f };
    areaLight.AxisVAndExtent = { 0.0f, 0.0f, 1.0f, 3.0f };
    areaLight.ColorAndIntensity = { 1.0f, 0.82f, 0.55f, 6.0f };
    m_AreaLights.push_back(areaLight);

    BuildSceneLightGpuData();
    MarkDirectionalLightsDirty();
    MarkPointLightsDirty(0, m_PointLightGpuData.size());
    MarkAreaLightsDirty();
}

void RaytracingDemo::UpdateDynamicLights(float timeSeconds)
{
    const size_t pointLightCount = std::min(m_PointLights.size(), m_PointLightBaseY.size());
    for (size_t i = 0; i < pointLightCount; ++i)
    {
        const float radius = i < m_PointLightOrbitRadius.size() ? m_PointLightOrbitRadius[i] : 16.0f;
        const float speed = i < m_PointLightOrbitSpeed.size() ? m_PointLightOrbitSpeed[i] : 0.4f;
        const float phase = i < m_PointLightPhase.size() ? m_PointLightPhase[i] : 0.0f;
        const float angle = timeSeconds * speed + phase;
        m_PointLights[i].PositionWs.x = -12.0f + std::cos(angle) * radius;
        m_PointLights[i].PositionWs.y = m_PointLightBaseY[i] + std::sin(angle * 1.7f) * 0.75f;
        m_PointLights[i].PositionWs.z = 18.0f + std::sin(angle) * radius;
        UpdatePointLightGpuData(i);
    }

    MarkPointLightsDirty(0, std::min(pointLightCount, m_PointLightGpuData.size()));
}

namespace
{
    template<typename T>
    std::vector<T> CreateBufferCapacityData(const size_t count)
    {
        return std::vector<T>(std::max<size_t>(1, count));
    }

    template<typename T>
    void UploadGpuLightRange(
        CommandList& commandList,
        SharedUploadBuffer& uploadBuffer,
        StructuredBuffer& destination,
        const std::vector<T>& values,
        const size_t beginIndex,
        const size_t endIndex)
    {
        const size_t clampedBegin = std::min(beginIndex, values.size());
        const size_t clampedEnd = std::min(endIndex, values.size());
        if (clampedBegin >= clampedEnd)
        {
            return;
        }

        const size_t elementCount = clampedEnd - clampedBegin;
        const uint64_t destinationOffset = static_cast<uint64_t>(clampedBegin * sizeof(T));
        commandList.TransitionBarrier(destination, D3D12_RESOURCE_STATE_COPY_DEST);
        commandList.FlushResourceBarriers();
        uploadBuffer.Upload(commandList, destination, values.data() + clampedBegin, elementCount * sizeof(T), sizeof(T), destinationOffset);
    }
}

void RaytracingDemo::InitializeSceneLightBuffers(CommandList& commandList)
{
    m_LightUploadBuffer = std::make_unique<SharedUploadBuffer>();
    commandList.CopyStructuredBuffer(m_DirectionalLightBuffer, CreateBufferCapacityData<DirectionalLightData>(MaxDirectionalLights));
    commandList.CopyStructuredBuffer(m_PointLightBuffer, CreateBufferCapacityData<PointLightData>(MaxPointLights));
    commandList.CopyStructuredBuffer(m_AreaLightBuffer, CreateBufferCapacityData<AreaLightData>(MaxAreaLights));
}

void RaytracingDemo::BuildSceneLightGpuData()
{
    m_DirectionalLightGpuData.clear();
    m_PointLightGpuData.clear();
    m_AreaLightGpuData.clear();

    m_DirectionalLightGpuData.reserve(std::min<size_t>(m_DirectionalLights.size(), MaxDirectionalLights));
    for (const DirectionalLight& light : m_DirectionalLights)
    {
        if (m_DirectionalLightGpuData.size() >= MaxDirectionalLights)
        {
            break;
        }

        DirectionalLightData gpuLight{};
        gpuLight.DirectionAndAngularRadius = light.m_DirectionWs;
        gpuLight.ColorAndIntensity = light.m_Color;
        m_DirectionalLightGpuData.push_back(gpuLight);
    }

    m_PointLightGpuData.reserve(std::min<size_t>(m_PointLights.size(), MaxPointLights));
    for (const PointLight& light : m_PointLights)
    {
        if (m_PointLightGpuData.size() >= MaxPointLights)
        {
            break;
        }

        PointLightData gpuLight{};
        gpuLight.PositionAndRange = { light.PositionWs.x, light.PositionWs.y, light.PositionWs.z, light.Range };
        gpuLight.ColorAndIntensity = light.Color;
        gpuLight.Attenuation = { light.ConstantAttenuation, light.LinearAttenuation, light.QuadraticAttenuation, 0.0f };
        m_PointLightGpuData.push_back(gpuLight);
    }

    m_AreaLightGpuData.reserve(std::min<size_t>(m_AreaLights.size(), MaxAreaLights));
    for (const AreaLightData& light : m_AreaLights)
    {
        if (m_AreaLightGpuData.size() >= MaxAreaLights)
        {
            break;
        }
        m_AreaLightGpuData.push_back(light);
    }
}

void RaytracingDemo::UpdatePointLightGpuData(const size_t lightIndex)
{
    if (lightIndex >= m_PointLights.size() || lightIndex >= MaxPointLights)
    {
        return;
    }

    if (m_PointLightGpuData.size() <= lightIndex)
    {
        m_PointLightGpuData.resize(lightIndex + 1);
    }

    const PointLight& light = m_PointLights[lightIndex];
    PointLightData& gpuLight = m_PointLightGpuData[lightIndex];
    gpuLight.PositionAndRange = { light.PositionWs.x, light.PositionWs.y, light.PositionWs.z, light.Range };
    gpuLight.ColorAndIntensity = light.Color;
    gpuLight.Attenuation = { light.ConstantAttenuation, light.LinearAttenuation, light.QuadraticAttenuation, 0.0f };
}

void RaytracingDemo::MarkDirectionalLightsDirty()
{
    m_DirectionalLightsDirty = true;
}

void RaytracingDemo::MarkPointLightsDirty(const size_t beginIndex, const size_t endIndex)
{
    if (beginIndex >= endIndex)
    {
        return;
    }

    if (m_PointLightDirtyBegin == m_PointLightDirtyEnd)
    {
        m_PointLightDirtyBegin = beginIndex;
        m_PointLightDirtyEnd = endIndex;
        return;
    }

    m_PointLightDirtyBegin = std::min(m_PointLightDirtyBegin, beginIndex);
    m_PointLightDirtyEnd = std::max(m_PointLightDirtyEnd, endIndex);
}

void RaytracingDemo::MarkAreaLightsDirty()
{
    m_AreaLightsDirty = true;
}

void RaytracingDemo::UploadSceneLightBuffers(CommandList& commandList)
{
    Assert(m_LightUploadBuffer != nullptr, "Light upload buffer is not initialized.");

    m_LightUploadBuffer->BeginFrame();
    if (m_DirectionalLightsDirty)
    {
        UploadGpuLightRange(commandList, *m_LightUploadBuffer, m_DirectionalLightBuffer, m_DirectionalLightGpuData, 0, m_DirectionalLightGpuData.size());
        m_DirectionalLightsDirty = false;
    }

    if (m_PointLightDirtyBegin < m_PointLightDirtyEnd)
    {
        UploadGpuLightRange(commandList, *m_LightUploadBuffer, m_PointLightBuffer, m_PointLightGpuData, m_PointLightDirtyBegin, m_PointLightDirtyEnd);
        m_PointLightDirtyBegin = 0;
        m_PointLightDirtyEnd = 0;
    }

    if (m_AreaLightsDirty)
    {
        UploadGpuLightRange(commandList, *m_LightUploadBuffer, m_AreaLightBuffer, m_AreaLightGpuData, 0, m_AreaLightGpuData.size());
        m_AreaLightsDirty = false;
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
    if (m_AnimatePointLights)
    {
        UpdateDynamicLights(static_cast<float>(e.TotalTime));
        if (!IsDenoiserEnabled())
        {
            ResetAccumulation();
        }
    }

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
    if (m_AccumulationEnabled && !IsDenoiserEnabled())
    {
        ++m_AccumulationFrameIndex;
    }
    else
    {
        m_AccumulationFrameIndex = 0;
    }
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
            m_CameraController.Pitch = Clamp(m_CameraController.Pitch + e.RelY * m_MouseRotateSpeed, -90.0f, 90.0f);
            m_CameraController.Yaw += e.RelX * m_MouseRotateSpeed;
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
