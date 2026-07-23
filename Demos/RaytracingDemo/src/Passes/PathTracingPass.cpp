#include <Passes/RaytracingDemoPasses.h>

#include <Passes/RaytracingDemoPassResources.h>
#include <RaytracingDemo.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>
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

            if (demo.m_PathTracingBackend == RaytracingDemo::PathTracingBackend::InlineRayQuery)
            {
                Assert(demo.m_Textures.size() <= RaytracingDemo::MaxInlineRayTracingTextures, "Inline ray tracing texture count exceeds the fixed descriptor layout.");
                Assert(demo.m_RayTracingAccelerationStructure.GetMeshes().size() <= RaytracingDemo::MaxInlineRayTracingGeometryBuffers, "Inline ray tracing mesh count exceeds the fixed descriptor layout.");

                demo.m_RootSignature->Bind(cmd);
                demo.m_InlinePathTracingShader->SetComputeConstantBuffer(cmd, sizeof(camera), &camera);
                demo.m_InlinePathTracingShader->SetAccelerationStructure(cmd, demo.m_RayTracingAccelerationStructure);
                demo.m_InlinePathTracingShader->SetPipelineShaderResourceView(cmd, RaytracingDemoRenderGraph::INLINE_SRV_GBUFFER_ALBEDO_OCCLUSION, ShaderResourceView(albedoOcclusion));
                demo.m_InlinePathTracingShader->SetPipelineShaderResourceView(cmd, RaytracingDemoRenderGraph::INLINE_SRV_GBUFFER_SPECULAR_SMOOTHNESS, ShaderResourceView(specularSmoothness));
                demo.m_InlinePathTracingShader->SetPipelineShaderResourceView(cmd, RaytracingDemoRenderGraph::INLINE_SRV_GBUFFER_NORMAL, ShaderResourceView(normal));
                demo.m_InlinePathTracingShader->SetPipelineShaderResourceView(cmd, RaytracingDemoRenderGraph::INLINE_SRV_GBUFFER_EMISSION_METALLIC, ShaderResourceView(emissionMetallic));
                demo.m_InlinePathTracingShader->SetPipelineShaderResourceView(cmd, RaytracingDemoRenderGraph::INLINE_SRV_GBUFFER_POSITION, ShaderResourceView(position));
                demo.m_InlinePathTracingShader->SetPipelineShaderResourceView(cmd, RaytracingDemoRenderGraph::INLINE_SRV_DEPTH, ShaderResourceView(depth, 0, 1, RaytracingDemoRenderGraph::CreateDepthSrvDesc()));
                demo.m_InlinePathTracingShader->SetPipelineShaderResourceView(cmd, RaytracingDemoRenderGraph::INLINE_SRV_SKYBOX, ShaderResourceView(demo.m_SkyboxTexture, RaytracingDemoRenderGraph::CreateSkyboxSrvDesc(*demo.m_SkyboxTexture)));
                demo.m_InlinePathTracingShader->SetPipelineShaderResourceView(cmd, RaytracingDemoRenderGraph::INLINE_SRV_MATERIALS, demo.m_MaterialBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                demo.m_InlinePathTracingShader->SetPipelineShaderResourceView(cmd, RaytracingDemoRenderGraph::INLINE_SRV_GEOMETRIES, demo.m_GeometryBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                demo.m_InlinePathTracingShader->SetPipelineShaderResourceView(cmd, RaytracingDemoRenderGraph::INLINE_SRV_DIRECTIONAL_LIGHTS, demo.m_DirectionalLightBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                demo.m_InlinePathTracingShader->SetPipelineShaderResourceView(cmd, RaytracingDemoRenderGraph::INLINE_SRV_POINT_LIGHTS, demo.m_PointLightBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                demo.m_InlinePathTracingShader->SetPipelineShaderResourceView(cmd, RaytracingDemoRenderGraph::INLINE_SRV_AREA_LIGHTS, demo.m_AreaLightBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

                for (uint32_t textureIndex = 0; textureIndex < std::min<uint32_t>(static_cast<uint32_t>(demo.m_Textures.size()), RaytracingDemo::MaxInlineRayTracingTextures); ++textureIndex)
                {
                    demo.m_InlinePathTracingShader->SetPipelineShaderResourceView(cmd, RaytracingDemoRenderGraph::INLINE_SRV_TEXTURES_BEGIN + textureIndex, ShaderResourceView(demo.m_Textures[textureIndex]));
                }

                const uint32_t inlineIndexBuffersBegin = RaytracingDemoRenderGraph::INLINE_SRV_VERTEX_BUFFERS_BEGIN + RaytracingDemo::MaxInlineRayTracingGeometryBuffers;
                const std::vector<std::shared_ptr<Mesh>>& meshes = demo.m_RayTracingAccelerationStructure.GetMeshes();
                const uint32_t meshCount = std::min<uint32_t>(static_cast<uint32_t>(meshes.size()), RaytracingDemo::MaxInlineRayTracingGeometryBuffers);
                for (uint32_t meshIndex = 0; meshIndex < meshCount; ++meshIndex)
                {
                    const Mesh& mesh = *meshes[meshIndex];
                    demo.m_InlinePathTracingShader->SetPipelineShaderResourceView(cmd, RaytracingDemoRenderGraph::INLINE_SRV_VERTEX_BUFFERS_BEGIN + meshIndex, mesh.GetVertexBuffer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    demo.m_InlinePathTracingShader->SetPipelineShaderResourceView(cmd, inlineIndexBuffersBegin + meshIndex, mesh.GetIndexBuffer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                }

                demo.m_InlinePathTracingShader->SetUnorderedAccessView(cmd, 0, UnorderedAccessView(output));
                demo.m_InlinePathTracingShader->SetUnorderedAccessView(cmd, 1, UnorderedAccessView(accumulation));
                demo.m_InlinePathTracingShader->SetUnorderedAccessView(cmd, 2, UnorderedAccessView(nrdNoisyRadiance));
                demo.m_InlinePathTracingShader->Bind(cmd);
                cmd.Dispatch((camera.Width + 7u) / 8u, (camera.Height + 7u) / 8u, 1u);
            }
            else
            {
                demo.m_RayTracingShader->SetOutputTexture("Output", output);
                demo.m_RayTracingShader->SetOutputTexture("Accumulation", accumulation);
                demo.m_RayTracingShader->SetOutputTexture("NrdNoisyRadiance", nrdNoisyRadiance);
                demo.m_RayTracingShader->SetTextureArray("GBufferTextures", gBufferTextures);
                demo.m_RayTracingShader->SetTextureArray("DepthTexture", { depth }, { RaytracingDemoRenderGraph::CreateDepthSrvDesc() });
                demo.m_RayTracingShader->SetStructuredBuffer("DirectionalLights", demo.m_DirectionalLightBuffer);
                demo.m_RayTracingShader->SetStructuredBuffer("PointLights", demo.m_PointLightBuffer);
                demo.m_RayTracingShader->SetStructuredBuffer("AreaLights", demo.m_AreaLightBuffer);
                demo.m_RayTracingShader->SetConstantBufferData("CameraConstants", &camera, sizeof(camera));
                demo.m_RayTracingShader->Dispatch(cmd, "RayGen", camera.Width, camera.Height);
            }
        });
}
