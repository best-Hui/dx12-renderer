#include "RenderGraph.User.h"

#include <RaytracingDemo.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/Texture.h>
#include <DX12Library/Window.h>
#include <Framework/Mesh.h>
#include <Framework/ShaderResourceView.h>

#include <d3dx12.h>

#include <algorithm>
#include <vector>

using namespace DirectX;

namespace
{
    constexpr FLOAT GBUFFER_CLEAR_COLOR[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    constexpr FLOAT OUTPUT_CLEAR_COLOR[] = { 0.4f, 0.6f, 0.9f, 1.0f };
    constexpr DXGI_FORMAT GBUFFER_FORMAT = DXGI_FORMAT_R16G16B16A16_FLOAT;
    constexpr DXGI_FORMAT ACCUMULATION_FORMAT = DXGI_FORMAT_R32G32B32A32_FLOAT;
    constexpr DXGI_FORMAT DEPTH_FORMAT = DXGI_FORMAT_D32_FLOAT;
    constexpr DXGI_FORMAT OUTPUT_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;

    class RaytracingDemoResourceIds
    {
    public:
        static inline const RenderGraph::ResourceId GBufferBaseColor = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.GBufferBaseColor");
        static inline const RenderGraph::ResourceId GBufferSpecular = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.GBufferSpecular");
        static inline const RenderGraph::ResourceId GBufferNormalRoughness = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.GBufferNormalRoughness");
        static inline const RenderGraph::ResourceId GBufferPositionMetallic = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.GBufferPositionMetallic");
        static inline const RenderGraph::ResourceId Accumulation = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.Accumulation");
        static inline const RenderGraph::ResourceId DepthBuffer = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.DepthBuffer");

        static inline const RenderGraph::ResourceId SetupFinishedToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.SetupFinished");
        static inline const RenderGraph::ResourceId GBufferFinishedToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.GBufferFinished");
        static inline const RenderGraph::ResourceId SkyboxFinishedToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.SkyboxFinished");
        static inline const RenderGraph::ResourceId RayTracingFinishedToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.RayTracingFinished");
    };

    D3D12_SHADER_RESOURCE_VIEW_DESC CreateSkyboxSrvDesc(const Texture& skybox)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = skybox.GetD3D12ResourceDesc().Format;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.TextureCube.MipLevels = 1;
        srvDesc.TextureCube.MostDetailedMip = 0;
        srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
        return srvDesc;
    }

}

std::unique_ptr<RenderGraph::RenderGraphRoot> RenderGraph::User::Create(RaytracingDemo& demo, CommandList& commandList)
{
    using namespace RenderGraph;

    std::vector<std::unique_ptr<RenderPass>> renderPasses;

    renderPasses.emplace_back(RenderPass::Create(
        L"Setup",
        {},
        {
            { RaytracingDemoResourceIds::SetupFinishedToken, OutputType::Token }
        },
        [&demo](const RenderContext&, CommandList& cmd)
        {
            demo.m_RootSignature->Bind(cmd);
            demo.m_RootSignature->SetPipelineConstantBuffer(cmd, demo.BuildPipelineConstants());
        }));

    renderPasses.emplace_back(RenderPass::Create(
        L"GBuffer",
        {
            { RaytracingDemoResourceIds::SetupFinishedToken, InputType::Token }
        },
        {
            { RaytracingDemoResourceIds::GBufferBaseColor, OutputType::RenderTarget },
            { RaytracingDemoResourceIds::GBufferSpecular, OutputType::RenderTarget },
            { RaytracingDemoResourceIds::GBufferNormalRoughness, OutputType::RenderTarget },
            { RaytracingDemoResourceIds::GBufferPositionMetallic, OutputType::RenderTarget },
            { RaytracingDemoResourceIds::DepthBuffer, OutputType::DepthWrite },
            { RaytracingDemoResourceIds::GBufferFinishedToken, OutputType::Token },
        },
        [&demo](const RenderContext&, CommandList& cmd)
        {
            demo.m_RootSignature->Bind(cmd);
            demo.m_RootSignature->SetPipelineConstantBuffer(cmd, demo.BuildPipelineConstants());
            demo.m_GBufferShader->Bind(cmd);

            const XMMATRIX viewProjection = demo.m_Camera.GetViewMatrix() * demo.m_Camera.GetProjectionMatrix();
            for (const RaytracingDemo::SceneObject& object : demo.m_SceneObjects)
            {
                const RaytracingDemo::MaterialData& material = demo.m_Materials[object.MaterialIndex];

                RaytracingDemo::ModelConstants modelConstants{};
                modelConstants.Model = object.WorldMatrix;
                modelConstants.ModelViewProjection = object.WorldMatrix * viewProjection;
                modelConstants.InverseTransposeModel = XMMatrixTranspose(XMMatrixInverse(nullptr, object.WorldMatrix));
                demo.m_RootSignature->SetModelConstantBuffer(cmd, modelConstants);

                RaytracingDemo::GBufferMaterialConstants materialConstants{};
                materialConstants.Diffuse = material.Diffuse;
                materialConstants.Specular = material.Specular;
                materialConstants.TilingOffset = material.TilingOffset;
                materialConstants.Metallic = material.Metallic;
                materialConstants.Roughness = material.Roughness;
                demo.m_RootSignature->SetMaterialConstantBuffer(cmd, sizeof(materialConstants), &materialConstants);
                demo.m_RootSignature->SetMaterialShaderResourceView(cmd, 0, ShaderResourceView(demo.m_Textures[material.DiffuseTextureIndex]));

                object.Model->Draw(cmd);
            }

            demo.m_GBufferShader->Unbind(cmd);
        }));

    renderPasses.emplace_back(RenderPass::Create(
        L"Skybox",
        {
            { RaytracingDemoResourceIds::GBufferFinishedToken, InputType::Token },
        },
        {
            { ResourceIds::GRAPH_OUTPUT, OutputType::RenderTarget },
            { RaytracingDemoResourceIds::DepthBuffer, OutputType::DepthRead },
            { RaytracingDemoResourceIds::SkyboxFinishedToken, OutputType::Token },
        },
        [&demo](const RenderContext&, CommandList& cmd)
        {
            demo.m_RootSignature->Bind(cmd);
            demo.m_RootSignature->SetPipelineConstantBuffer(cmd, demo.BuildPipelineConstants());

            const XMMATRIX viewProjection = demo.m_Camera.GetViewMatrix() * demo.m_Camera.GetProjectionMatrix();
            const XMMATRIX modelMatrix = XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslationFromVector(demo.m_Camera.GetTranslation());
            RaytracingDemo::ModelConstants modelConstants{};
            modelConstants.Model = modelMatrix;
            modelConstants.ModelViewProjection = modelMatrix * viewProjection;
            modelConstants.InverseTransposeModel = XMMatrixTranspose(XMMatrixInverse(nullptr, modelMatrix));
            demo.m_RootSignature->SetModelConstantBuffer(cmd, modelConstants);

            const auto skyboxSrv = ShaderResourceView(demo.m_SkyboxTexture, CreateSkyboxSrvDesc(*demo.m_SkyboxTexture));
            demo.m_RootSignature->SetMaterialShaderResourceView(cmd, 0, skyboxSrv);

            demo.m_SkyboxShader->Bind(cmd);
            demo.m_SkyboxMesh->Draw(cmd);
            demo.m_SkyboxShader->Unbind(cmd);
        }));

    renderPasses.emplace_back(RenderPass::Create(
        L"Ray Tracing",
        {
            { RaytracingDemoResourceIds::GBufferFinishedToken, InputType::Token },
            { RaytracingDemoResourceIds::SkyboxFinishedToken, InputType::Token },
            { RaytracingDemoResourceIds::GBufferBaseColor, InputType::ShaderResource },
            { RaytracingDemoResourceIds::GBufferSpecular, InputType::ShaderResource },
            { RaytracingDemoResourceIds::GBufferNormalRoughness, InputType::ShaderResource },
            { RaytracingDemoResourceIds::GBufferPositionMetallic, InputType::ShaderResource },
        },
        {
            { ResourceIds::GRAPH_OUTPUT, OutputType::UnorderedAccess },
            { RaytracingDemoResourceIds::Accumulation, OutputType::UnorderedAccess },
            { RaytracingDemoResourceIds::RayTracingFinishedToken, OutputType::Token },
        },
        [&demo](const RenderContext& context, CommandList& cmd)
        {
            const auto& output = context.m_ResourcePool->GetTexture(ResourceIds::GRAPH_OUTPUT);
            const auto& accumulation = context.m_ResourcePool->GetTexture(RaytracingDemoResourceIds::Accumulation);
            const auto& baseColor = context.m_ResourcePool->GetTexture(RaytracingDemoResourceIds::GBufferBaseColor);
            const auto& specular = context.m_ResourcePool->GetTexture(RaytracingDemoResourceIds::GBufferSpecular);
            const auto& normalRoughness = context.m_ResourcePool->GetTexture(RaytracingDemoResourceIds::GBufferNormalRoughness);
            const auto& positionMetallic = context.m_ResourcePool->GetTexture(RaytracingDemoResourceIds::GBufferPositionMetallic);

            std::vector<std::shared_ptr<Texture>> gBufferTextures = { baseColor, specular, normalRoughness, positionMetallic };

            RaytracingDemo::CameraConstants camera = demo.BuildCameraConstants();
            camera.Width = context.m_Metadata.m_ScreenWidth;
            camera.Height = context.m_Metadata.m_ScreenHeight;
            camera.FrameIndex = static_cast<uint32_t>(context.m_Metadata.m_FrameIndex);

            demo.m_RayTracingShader->SetOutputTexture("Output", output);
            demo.m_RayTracingShader->SetOutputTexture("Accumulation", accumulation);
            demo.m_RayTracingShader->SetTextureArray("GBufferTextures", gBufferTextures);
            demo.m_RayTracingShader->SetConstantBufferData("CameraConstants", &camera, sizeof(camera));
            demo.m_RayTracingShader->Dispatch(cmd, "RayGen", camera.Width, camera.Height);
        }));

    renderPasses.emplace_back(RenderPass::Create(
        L"ImGui",
        {
            { RaytracingDemoResourceIds::RayTracingFinishedToken, InputType::Token },
        },
        {
            { ResourceIds::GRAPH_OUTPUT, OutputType::RenderTarget },
        },
        [&demo](const RenderContext&, CommandList& cmd)
        {
            demo.m_ImGui->DrawToRenderTarget(cmd);
        }));

    const RenderMetadataExpression<uint32_t> renderWidthExpression = [](const RenderMetadata& metadata) { return metadata.m_ScreenWidth; };
    const RenderMetadataExpression<uint32_t> renderHeightExpression = [](const RenderMetadata& metadata) { return metadata.m_ScreenHeight; };

    std::vector<TextureDescription> textures = {
        { ResourceIds::GRAPH_OUTPUT, renderWidthExpression, renderHeightExpression, OUTPUT_FORMAT, OUTPUT_CLEAR_COLOR, ResourceInitAction::Clear },
        { RaytracingDemoResourceIds::GBufferBaseColor, renderWidthExpression, renderHeightExpression, GBUFFER_FORMAT, GBUFFER_CLEAR_COLOR, ResourceInitAction::Clear },
        { RaytracingDemoResourceIds::GBufferSpecular, renderWidthExpression, renderHeightExpression, GBUFFER_FORMAT, GBUFFER_CLEAR_COLOR, ResourceInitAction::Clear },
        { RaytracingDemoResourceIds::GBufferNormalRoughness, renderWidthExpression, renderHeightExpression, GBUFFER_FORMAT, GBUFFER_CLEAR_COLOR, ResourceInitAction::Clear },
        { RaytracingDemoResourceIds::GBufferPositionMetallic, renderWidthExpression, renderHeightExpression, GBUFFER_FORMAT, GBUFFER_CLEAR_COLOR, ResourceInitAction::Clear },
        { RaytracingDemoResourceIds::Accumulation, renderWidthExpression, renderHeightExpression, ACCUMULATION_FORMAT, GBUFFER_CLEAR_COLOR, ResourceInitAction::Discard },
        { RaytracingDemoResourceIds::DepthBuffer, renderWidthExpression, renderHeightExpression, DEPTH_FORMAT, { 1.0f, 0u }, ResourceInitAction::Clear },
    };

    std::vector<BufferDescription> buffers;
    std::vector<TokenDescription> tokens = {
        { RaytracingDemoResourceIds::SetupFinishedToken },
        { RaytracingDemoResourceIds::GBufferFinishedToken },
        { RaytracingDemoResourceIds::SkyboxFinishedToken },
        { RaytracingDemoResourceIds::RayTracingFinishedToken },
    };

    return std::make_unique<RenderGraphRoot>(std::move(renderPasses), std::move(textures), std::move(buffers), std::move(tokens));
}
