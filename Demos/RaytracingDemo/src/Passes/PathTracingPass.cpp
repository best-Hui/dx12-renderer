#include <Passes/RaytracingDemoPasses.h>

#include <Passes/RaytracingDemoPassResources.h>
#include <RaytracingDemo.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>
#include <Framework/Mesh.h>
#include <Framework/ShaderResourceView.h>
#include <Framework/UnorderedAccessView.h>
#include <RenderGraph/RenderPass.h>

#include <vector>

namespace
{
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;

    struct GBufferResources
    {
        std::shared_ptr<Texture> AlbedoOcclusion;
        std::shared_ptr<Texture> SpecularSmoothness;
        std::shared_ptr<Texture> Normal;
        std::shared_ptr<Texture> EmissionMetallic;
        std::shared_ptr<Texture> Position;
        std::shared_ptr<Texture> Depth;
    };

    GBufferResources GetGBufferResources(const RenderGraph::RenderContext& context)
    {
        return {
            context.m_ResourcePool->GetTexture(DemoResourceIds::GBufferAlbedoOcclusion),
            context.m_ResourcePool->GetTexture(DemoResourceIds::GBufferSpecularSmoothness),
            context.m_ResourcePool->GetTexture(DemoResourceIds::GBufferNormal),
            context.m_ResourcePool->GetTexture(DemoResourceIds::GBufferEmissionMetallic),
            context.m_ResourcePool->GetTexture(DemoResourceIds::GBufferPosition),
            context.m_ResourcePool->GetTexture(DemoResourceIds::DepthBuffer),
        };
    }

    bool HasSrvBinding(const ComputeShader& shader, const std::string& name)
    {
        return shader.GetShaderMetadata().m_ShaderResourceViewsNameCache.find(name) !=
            shader.GetShaderMetadata().m_ShaderResourceViewsNameCache.end();
    }

}

struct RaytracingDemoPassAccess
{
    static RaytracingDemo::CameraConstants BuildPassCameraConstants(RaytracingDemo& demo, const RenderGraph::RenderContext& context)
    {
        RaytracingDemo::CameraConstants camera = demo.BuildCameraConstants();
        camera.Width = context.m_Metadata.m_ScreenWidth;
        camera.Height = context.m_Metadata.m_ScreenHeight;
        camera.FrameIndex = static_cast<uint32_t>(context.m_Metadata.m_FrameIndex);
        return camera;
    }

    static void BindInlinePathTracingInputs(
        RaytracingDemo& demo,
        CommandList& cmd,
        ComputeShader& shader,
        const GBufferResources& gbuffer,
        const RaytracingDemo::CameraConstants& camera)
    {
        const uint32_t textureCount = static_cast<uint32_t>(demo.m_Textures.size());
        const std::vector<std::shared_ptr<Mesh>>& meshes = demo.m_RayTracingAccelerationStructure.GetMeshes();
        const uint32_t meshCount = static_cast<uint32_t>(meshes.size());

        Assert(textureCount <= demo.m_RayTracingSceneResourceLayout.TextureDescriptorCapacity, "Ray tracing texture descriptors exceed the scene descriptor table capacity.");
        Assert(meshCount <= demo.m_RayTracingSceneResourceLayout.GeometryDescriptorCapacity, "Ray tracing geometry descriptors exceed the scene descriptor table capacity.");

        shader.Bind(cmd);
        shader.SetConstantBuffer(cmd, "CameraConstants", camera);
        shader.SetAccelerationStructure(cmd, demo.m_RayTracingAccelerationStructure);
        if (HasSrvBinding(shader, "GBufferTextures"))
        {
            shader.SetShaderResourceView(cmd, "GBufferTextures", 0u, ShaderResourceView(gbuffer.AlbedoOcclusion));
            shader.SetShaderResourceView(cmd, "GBufferTextures", 1u, ShaderResourceView(gbuffer.SpecularSmoothness));
            shader.SetShaderResourceView(cmd, "GBufferTextures", 2u, ShaderResourceView(gbuffer.Normal));
            shader.SetShaderResourceView(cmd, "GBufferTextures", 3u, ShaderResourceView(gbuffer.EmissionMetallic));
            shader.SetShaderResourceView(cmd, "GBufferTextures", 4u, ShaderResourceView(gbuffer.Position));
        }
        if (HasSrvBinding(shader, "DepthTexture"))
        {
            shader.SetShaderResourceView(cmd, "DepthTexture", ShaderResourceView::DepthAsFloat(gbuffer.Depth));
        }
        if (HasSrvBinding(shader, "Skybox"))
        {
            shader.SetShaderResourceView(cmd, "Skybox", ShaderResourceView::TextureCube(demo.m_SkyboxTexture));
        }
        if (HasSrvBinding(shader, "Materials"))
        {
            shader.SetShaderResourceView(cmd, "Materials", 0u, demo.m_MaterialBuffer);
        }
        if (HasSrvBinding(shader, "Geometries"))
        {
            shader.SetShaderResourceView(cmd, "Geometries", 0u, demo.m_GeometryBuffer);
        }
        if (HasSrvBinding(shader, "DirectionalLights"))
        {
            shader.SetShaderResourceView(cmd, "DirectionalLights", 0u, demo.m_DirectionalLightBuffer);
        }
        if (HasSrvBinding(shader, "PointLights"))
        {
            shader.SetShaderResourceView(cmd, "PointLights", 0u, demo.m_PointLightBuffer);
        }
        if (HasSrvBinding(shader, "AreaLights"))
        {
            shader.SetShaderResourceView(cmd, "AreaLights", 0u, demo.m_AreaLightBuffer);
        }

        if (HasSrvBinding(shader, "Textures"))
        {
            for (uint32_t textureIndex = 0; textureIndex < textureCount; ++textureIndex)
            {
                shader.SetShaderResourceView(cmd, "Textures", textureIndex, ShaderResourceView(demo.m_Textures[textureIndex]));
            }
        }

        if (HasSrvBinding(shader, "VertexBuffers"))
        {
            for (uint32_t meshIndex = 0; meshIndex < meshCount; ++meshIndex)
            {
                const Mesh& mesh = *meshes[meshIndex];
                shader.SetShaderResourceView(cmd, "VertexBuffers", meshIndex, mesh.GetVertexBuffer());
            }
        }

        if (HasSrvBinding(shader, "IndexBuffers"))
        {
            for (uint32_t meshIndex = 0; meshIndex < meshCount; ++meshIndex)
            {
                const Mesh& mesh = *meshes[meshIndex];
                shader.SetShaderResourceView(cmd, "IndexBuffers", meshIndex, mesh.GetIndexBuffer());
            }
        }
    }

    static void BindDxrPathTracingInputs(
        RaytracingDemo& demo,
        RayTracingBindingSet& shader,
        const GBufferResources& gbuffer,
        const RaytracingDemo::CameraConstants& camera)
    {
        shader.SetTexture("GBufferTextures", 0u, ShaderResourceView(gbuffer.AlbedoOcclusion));
        shader.SetTexture("GBufferTextures", 1u, ShaderResourceView(gbuffer.SpecularSmoothness));
        shader.SetTexture("GBufferTextures", 2u, ShaderResourceView(gbuffer.Normal));
        shader.SetTexture("GBufferTextures", 3u, ShaderResourceView(gbuffer.EmissionMetallic));
        shader.SetTexture("GBufferTextures", 4u, ShaderResourceView(gbuffer.Position));
        shader.SetTexture("DepthTexture", ShaderResourceView::DepthAsFloat(gbuffer.Depth));
        shader.SetBuffer("DirectionalLights", demo.m_DirectionalLightBuffer);
        shader.SetBuffer("PointLights", demo.m_PointLightBuffer);
        shader.SetBuffer("AreaLights", demo.m_AreaLightBuffer);
        shader.SetConstantBufferData("CameraConstants", &camera, sizeof(camera));
    }

    static void BindCompositeInputs(
        RaytracingDemo& demo,
        CommandList& cmd,
        const RenderGraph::RenderContext& context,
        const GBufferResources& gbuffer,
        const RaytracingDemo::CameraConstants& camera)
    {
        const auto& directLighting = context.m_ResourcePool->GetTexture(DemoResourceIds::DirectLighting);
        const auto& indirectLighting = context.m_ResourcePool->GetTexture(DemoResourceIds::IndirectLighting);
        const auto& output = context.m_ResourcePool->GetTexture(RenderGraph::ResourceIds::GRAPH_OUTPUT);
        const auto& accumulation = context.m_ResourcePool->GetTexture(DemoResourceIds::Accumulation);
        const auto& nrdNoisyRadiance = context.m_ResourcePool->GetTexture(DemoResourceIds::NrdNoisyRadiance);

        demo.m_LightingCompositeShader->Bind(cmd);
        demo.m_LightingCompositeShader->SetConstantBuffer(cmd, "CameraConstants", camera);
        demo.m_LightingCompositeShader->SetShaderResourceView(cmd, "GBufferTextures", 0u, ShaderResourceView(gbuffer.AlbedoOcclusion));
        demo.m_LightingCompositeShader->SetShaderResourceView(cmd, "GBufferTextures", 1u, ShaderResourceView(gbuffer.SpecularSmoothness));
        demo.m_LightingCompositeShader->SetShaderResourceView(cmd, "GBufferTextures", 2u, ShaderResourceView(gbuffer.Normal));
        demo.m_LightingCompositeShader->SetShaderResourceView(cmd, "GBufferTextures", 3u, ShaderResourceView(gbuffer.EmissionMetallic));
        demo.m_LightingCompositeShader->SetShaderResourceView(cmd, "GBufferTextures", 4u, ShaderResourceView(gbuffer.Position));
        demo.m_LightingCompositeShader->SetShaderResourceView(cmd, "DepthTexture", ShaderResourceView::DepthAsFloat(gbuffer.Depth));
        demo.m_LightingCompositeShader->SetShaderResourceView(cmd, "Skybox", ShaderResourceView::TextureCube(demo.m_SkyboxTexture));
        demo.m_LightingCompositeShader->SetShaderResourceView(cmd, "DirectLightingTexture", ShaderResourceView(directLighting));
        demo.m_LightingCompositeShader->SetShaderResourceView(cmd, "IndirectLightingTexture", ShaderResourceView(indirectLighting));
        demo.m_LightingCompositeShader->SetUnorderedAccessView(cmd, "Output", UnorderedAccessView(output));
        demo.m_LightingCompositeShader->SetUnorderedAccessView(cmd, "Accumulation", UnorderedAccessView(accumulation));
        demo.m_LightingCompositeShader->SetUnorderedAccessView(cmd, "NrdNoisyRadiance", UnorderedAccessView(nrdNoisyRadiance));
    }
};

std::unique_ptr<RenderGraph::RenderPass> RaytracingDemoPasses::Builder::CreateDirectLightingPass(RaytracingDemo& demo)
{
    using namespace RenderGraph;

    return RenderPass::Create(
        L"Direct Lighting",
        {
            { DemoResourceIds::BaseResourcesFinishedToken, InputType::Token },
            { DemoResourceIds::SkyboxFinishedToken, InputType::Token },
            { DemoResourceIds::GBufferAlbedoOcclusion, InputType::ShaderResource },
            { DemoResourceIds::GBufferSpecularSmoothness, InputType::ShaderResource },
            { DemoResourceIds::GBufferNormal, InputType::ShaderResource },
            { DemoResourceIds::GBufferEmissionMetallic, InputType::ShaderResource },
            { DemoResourceIds::GBufferPosition, InputType::ShaderResource },
            { DemoResourceIds::DepthBuffer, InputType::ShaderResource },
        },
        {
            { DemoResourceIds::DirectLighting, OutputType::UnorderedAccess },
            { DemoResourceIds::DirectLightingFinishedToken, OutputType::Token },
        },
        [&demo](const RenderContext& context, CommandList& cmd)
        {
            if (!demo.m_DirectLightingEnabled)
            {
                return;
            }

            const GBufferResources gbuffer = GetGBufferResources(context);
            RaytracingDemo::CameraConstants camera = RaytracingDemoPassAccess::BuildPassCameraConstants(demo, context);

            demo.EnsureRayTracingPipelines();
            if (demo.m_PathTracingBackend == RaytracingDemo::PathTracingBackend::InlineRayQuery)
            {
                RaytracingDemoPassAccess::BindInlinePathTracingInputs(demo, cmd, *demo.m_InlineDirectLightingShader, gbuffer, camera);
                demo.m_InlineDirectLightingShader->SetUnorderedAccessView(cmd, "DirectLighting", UnorderedAccessView(context.m_ResourcePool->GetTexture(DemoResourceIds::DirectLighting)));
                cmd.Dispatch(Math::DivideByMultiple(camera.Width, 8u), Math::DivideByMultiple(camera.Height, 8u), 1u);
            }
            else
            {
                demo.m_DirectRayTracingBindingSet->SetUnorderedAccessView("DirectLighting", UnorderedAccessView(context.m_ResourcePool->GetTexture(DemoResourceIds::DirectLighting)));
                RaytracingDemoPassAccess::BindDxrPathTracingInputs(demo, *demo.m_DirectRayTracingBindingSet, gbuffer, camera);
                demo.m_DirectRayTracingBindingSet->Dispatch(cmd, "DirectLightingRayGen", camera.Width, camera.Height);
            }
        });
}

std::unique_ptr<RenderGraph::RenderPass> RaytracingDemoPasses::Builder::CreateIndirectLightingPass(RaytracingDemo& demo)
{
    using namespace RenderGraph;

    return RenderPass::Create(
        L"Indirect Lighting",
        {
            { DemoResourceIds::DirectLightingFinishedToken, InputType::Token },
            { DemoResourceIds::GBufferAlbedoOcclusion, InputType::ShaderResource },
            { DemoResourceIds::GBufferSpecularSmoothness, InputType::ShaderResource },
            { DemoResourceIds::GBufferNormal, InputType::ShaderResource },
            { DemoResourceIds::GBufferEmissionMetallic, InputType::ShaderResource },
            { DemoResourceIds::GBufferPosition, InputType::ShaderResource },
            { DemoResourceIds::DepthBuffer, InputType::ShaderResource },
        },
        {
            { DemoResourceIds::IndirectLighting, OutputType::UnorderedAccess },
            { DemoResourceIds::IndirectLightingFinishedToken, OutputType::Token },
        },
        [&demo](const RenderContext& context, CommandList& cmd)
        {
            if (!demo.m_IndirectLightingEnabled)
            {
                return;
            }

            const GBufferResources gbuffer = GetGBufferResources(context);
            RaytracingDemo::CameraConstants camera = RaytracingDemoPassAccess::BuildPassCameraConstants(demo, context);

            demo.EnsureRayTracingPipelines();
            if (demo.m_PathTracingBackend == RaytracingDemo::PathTracingBackend::InlineRayQuery)
            {
                RaytracingDemoPassAccess::BindInlinePathTracingInputs(demo, cmd, *demo.m_InlineIndirectLightingShader, gbuffer, camera);
                demo.m_InlineIndirectLightingShader->SetUnorderedAccessView(cmd, "IndirectLighting", UnorderedAccessView(context.m_ResourcePool->GetTexture(DemoResourceIds::IndirectLighting)));
                cmd.Dispatch(Math::DivideByMultiple(camera.Width, 8u), Math::DivideByMultiple(camera.Height, 8u), 1u);
            }
            else
            {
                demo.m_IndirectRayTracingBindingSet->SetUnorderedAccessView("IndirectLighting", UnorderedAccessView(context.m_ResourcePool->GetTexture(DemoResourceIds::IndirectLighting)));
                RaytracingDemoPassAccess::BindDxrPathTracingInputs(demo, *demo.m_IndirectRayTracingBindingSet, gbuffer, camera);
                demo.m_IndirectRayTracingBindingSet->Dispatch(cmd, "IndirectLightingRayGen", camera.Width, camera.Height);
            }
        });
}

std::unique_ptr<RenderGraph::RenderPass> RaytracingDemoPasses::Builder::CreateLightingCompositePass(RaytracingDemo& demo)
{
    using namespace RenderGraph;

    return RenderPass::Create(
        L"Lighting Composite",
        {
            { DemoResourceIds::DirectLightingFinishedToken, InputType::Token },
            { DemoResourceIds::IndirectLightingFinishedToken, InputType::Token },
            { DemoResourceIds::DirectLighting, InputType::ShaderResource },
            { DemoResourceIds::IndirectLighting, InputType::ShaderResource },
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
            const GBufferResources gbuffer = GetGBufferResources(context);
            const RaytracingDemo::CameraConstants camera = RaytracingDemoPassAccess::BuildPassCameraConstants(demo, context);
            RaytracingDemoPassAccess::BindCompositeInputs(demo, cmd, context, gbuffer, camera);
            cmd.Dispatch(Math::DivideByMultiple(camera.Width, 8u), Math::DivideByMultiple(camera.Height, 8u), 1u);
        });
}
