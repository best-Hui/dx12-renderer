#include <Passes/RaytracingDemoPasses.h>

#include <Passes/RaytracingDemoPassResources.h>
#include <RaytracingDemo.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>
#include <Framework/CommonRootSignature.h>
#include <Framework/Mesh.h>
#include <Framework/ShaderResourceView.h>
#include <Framework/UnorderedAccessView.h>
#include <RenderGraph/RenderPass.h>

#include <algorithm>
#include <vector>

std::unique_ptr<RenderGraph::RenderPass> RaytracingDemoPasses::Builder::CreatePathTracingPass(RaytracingDemo& demo)
{
    using namespace RenderGraph;
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;

    return RenderPass::Create(
        L"Path Tracing",
        {
            { DemoResourceIds::GBufferFinishedToken, InputType::Token },
            { DemoResourceIds::SkyboxFinishedToken, InputType::Token },
            { DemoResourceIds::GBufferAlbedoOcclusion, InputType::ShaderResource },
            { DemoResourceIds::GBufferSpecularSmoothness, InputType::ShaderResource },
            { DemoResourceIds::GBufferNormal, InputType::ShaderResource },
            { DemoResourceIds::GBufferEmissionMetallic, InputType::ShaderResource },
            { DemoResourceIds::GBufferPosition, InputType::ShaderResource },
            { DemoResourceIds::DepthBuffer, InputType::ShaderResource },
        },
        {
            { RenderGraph::ResourceIds::GRAPH_OUTPUT, OutputType::UnorderedAccess },
            { DemoResourceIds::Accumulation, OutputType::UnorderedAccess },
            { DemoResourceIds::NrdNoisyRadiance, OutputType::UnorderedAccess },
            { DemoResourceIds::RayTracingFinishedToken, OutputType::Token },
        },
        [&demo](const RenderContext& context, CommandList& cmd)
        {
            const auto& output = context.m_ResourcePool->GetTexture(RenderGraph::ResourceIds::GRAPH_OUTPUT);
            const auto& accumulation = context.m_ResourcePool->GetTexture(DemoResourceIds::Accumulation);
            const auto& nrdNoisyRadiance = context.m_ResourcePool->GetTexture(DemoResourceIds::NrdNoisyRadiance);
            const auto& albedoOcclusion = context.m_ResourcePool->GetTexture(DemoResourceIds::GBufferAlbedoOcclusion);
            const auto& specularSmoothness = context.m_ResourcePool->GetTexture(DemoResourceIds::GBufferSpecularSmoothness);
            const auto& normal = context.m_ResourcePool->GetTexture(DemoResourceIds::GBufferNormal);
            const auto& emissionMetallic = context.m_ResourcePool->GetTexture(DemoResourceIds::GBufferEmissionMetallic);
            const auto& position = context.m_ResourcePool->GetTexture(DemoResourceIds::GBufferPosition);
            const auto& depth = context.m_ResourcePool->GetTexture(DemoResourceIds::DepthBuffer);

            std::vector<std::shared_ptr<Texture>> gBufferTextures = { albedoOcclusion, specularSmoothness, normal, emissionMetallic, position };

            RaytracingDemo::CameraConstants camera = demo.BuildCameraConstants();
            camera.Width = context.m_Metadata.m_ScreenWidth;
            camera.Height = context.m_Metadata.m_ScreenHeight;
            camera.FrameIndex = static_cast<uint32_t>(context.m_Metadata.m_FrameIndex);

            const uint32_t textureCount = static_cast<uint32_t>(demo.m_Textures.size());
            const std::vector<std::shared_ptr<Mesh>>& meshes = demo.m_RayTracingAccelerationStructure.GetMeshes();
            const uint32_t meshCount = static_cast<uint32_t>(meshes.size());

            demo.EnsureRayTracingPipelines();
            Assert(textureCount <= demo.m_RayTracingSceneResourceLayout.TextureDescriptorCapacity, "Ray tracing texture descriptors exceed the scene descriptor table capacity.");
            Assert(meshCount <= demo.m_RayTracingSceneResourceLayout.GeometryDescriptorCapacity, "Ray tracing geometry descriptors exceed the scene descriptor table capacity.");

            if (demo.m_PathTracingBackend == RaytracingDemo::PathTracingBackend::InlineRayQuery)
            {
                demo.m_InlinePathTracingShader->Bind(cmd);
                demo.m_InlinePathTracingShader->SetConstantBuffer(cmd, "CameraConstants", camera);
                demo.m_InlinePathTracingShader->SetAccelerationStructure(cmd, demo.m_RayTracingAccelerationStructure);
                demo.m_InlinePathTracingShader->SetShaderResourceView(cmd, "GBufferTextures", 0u, ShaderResourceView(albedoOcclusion));
                demo.m_InlinePathTracingShader->SetShaderResourceView(cmd, "GBufferTextures", 1u, ShaderResourceView(specularSmoothness));
                demo.m_InlinePathTracingShader->SetShaderResourceView(cmd, "GBufferTextures", 2u, ShaderResourceView(normal));
                demo.m_InlinePathTracingShader->SetShaderResourceView(cmd, "GBufferTextures", 3u, ShaderResourceView(emissionMetallic));
                demo.m_InlinePathTracingShader->SetShaderResourceView(cmd, "GBufferTextures", 4u, ShaderResourceView(position));
                demo.m_InlinePathTracingShader->SetShaderResourceView(cmd, "DepthTexture", ShaderResourceView::DepthAsFloat(depth));
                demo.m_InlinePathTracingShader->SetShaderResourceView(cmd, "Skybox", ShaderResourceView::TextureCube(demo.m_SkyboxTexture));
                demo.m_InlinePathTracingShader->SetShaderResourceView(cmd, "Materials", 0u, demo.m_MaterialBuffer);
                demo.m_InlinePathTracingShader->SetShaderResourceView(cmd, "Geometries", 0u, demo.m_GeometryBuffer);
                demo.m_InlinePathTracingShader->SetShaderResourceView(cmd, "DirectionalLights", 0u, demo.m_DirectionalLightBuffer);
                demo.m_InlinePathTracingShader->SetShaderResourceView(cmd, "PointLights", 0u, demo.m_PointLightBuffer);
                demo.m_InlinePathTracingShader->SetShaderResourceView(cmd, "AreaLights", 0u, demo.m_AreaLightBuffer);

                for (uint32_t textureIndex = 0; textureIndex < textureCount; ++textureIndex)
                {
                    demo.m_InlinePathTracingShader->SetShaderResourceView(cmd, "Textures", textureIndex, ShaderResourceView(demo.m_Textures[textureIndex]));
                }

                for (uint32_t meshIndex = 0; meshIndex < meshCount; ++meshIndex)
                {
                    const Mesh& mesh = *meshes[meshIndex];
                    demo.m_InlinePathTracingShader->SetShaderResourceView(cmd, "VertexBuffers", meshIndex, mesh.GetVertexBuffer());
                    demo.m_InlinePathTracingShader->SetShaderResourceView(cmd, "IndexBuffers", meshIndex, mesh.GetIndexBuffer());
                }

                demo.m_InlinePathTracingShader->SetUnorderedAccessView(cmd, "Output", UnorderedAccessView(output));
                demo.m_InlinePathTracingShader->SetUnorderedAccessView(cmd, "Accumulation", UnorderedAccessView(accumulation));
                demo.m_InlinePathTracingShader->SetUnorderedAccessView(cmd, "NrdNoisyRadiance", UnorderedAccessView(nrdNoisyRadiance));
                cmd.Dispatch((camera.Width + 7u) / 8u, (camera.Height + 7u) / 8u, 1u);
            }
            else
            {
                demo.m_RayTracingShader->SetOutputTexture("Output", output);
                demo.m_RayTracingShader->SetOutputTexture("Accumulation", accumulation);
                demo.m_RayTracingShader->SetOutputTexture("NrdNoisyRadiance", nrdNoisyRadiance);
                demo.m_RayTracingShader->SetTextureArray("GBufferTextures", gBufferTextures);
                demo.m_RayTracingShader->SetTextureArray("DepthTexture", { ShaderResourceView::DepthAsFloat(depth) });
                demo.m_RayTracingShader->SetStructuredBuffer("DirectionalLights", demo.m_DirectionalLightBuffer);
                demo.m_RayTracingShader->SetStructuredBuffer("PointLights", demo.m_PointLightBuffer);
                demo.m_RayTracingShader->SetStructuredBuffer("AreaLights", demo.m_AreaLightBuffer);
                demo.m_RayTracingShader->SetConstantBufferData("CameraConstants", &camera, sizeof(camera));
                demo.m_RayTracingShader->Dispatch(cmd, "RayGen", camera.Width, camera.Height);
            }
        });
}
