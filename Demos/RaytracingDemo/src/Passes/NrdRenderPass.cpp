#include <Passes/RaytracingDemoPasses.h>

#include <Passes/NrdPass.h>
#include <Passes/RaytracingDemoPassResources.h>
#include <RaytracingDemo.h>

#include <RenderGraph/RenderPass.h>

std::unique_ptr<RenderGraph::RenderPass> RaytracingDemoPasses::Builder::CreateNrdPass(RaytracingDemo& demo)
{
    using namespace RenderGraph;
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;

    return RenderPass::Create(
        L"NRD Denoise",
        {
            { DemoResourceIds::RayTracingFinishedToken, InputType::Token },
            { DemoResourceIds::NrdNoisyRadiance, InputType::ShaderResource },
            { DemoResourceIds::GBufferAlbedoOcclusion, InputType::ShaderResource },
            { DemoResourceIds::GBufferSpecularSmoothness, InputType::ShaderResource },
            { DemoResourceIds::GBufferNormal, InputType::ShaderResource },
            { DemoResourceIds::GBufferEmissionMetallic, InputType::ShaderResource },
            { DemoResourceIds::GBufferPosition, InputType::ShaderResource },
            { DemoResourceIds::DepthBuffer, InputType::ShaderResource },
        },
        {
            { DemoResourceIds::NrdNormalRoughness, OutputType::UnorderedAccess },
            { DemoResourceIds::NrdViewZ, OutputType::UnorderedAccess },
            { DemoResourceIds::NrdMotion, OutputType::UnorderedAccess },
            { DemoResourceIds::NrdDenoisedRadiance, OutputType::UnorderedAccess },
            { RenderGraph::ResourceIds::GRAPH_OUTPUT, OutputType::UnorderedAccess },
            { DemoResourceIds::NrdFinishedToken, OutputType::Token },
        },
        [&demo](const RenderContext& context, CommandList& cmd)
        {
            if (demo.m_NrdPass == nullptr)
            {
                return;
            }

            demo.m_NrdPass->Execute(
                demo,
                cmd,
                context.m_ResourcePool->GetTexture(DemoResourceIds::NrdNoisyRadiance),
                context.m_ResourcePool->GetTexture(DemoResourceIds::GBufferAlbedoOcclusion),
                context.m_ResourcePool->GetTexture(DemoResourceIds::GBufferSpecularSmoothness),
                context.m_ResourcePool->GetTexture(DemoResourceIds::GBufferNormal),
                context.m_ResourcePool->GetTexture(DemoResourceIds::GBufferEmissionMetallic),
                context.m_ResourcePool->GetTexture(DemoResourceIds::GBufferPosition),
                context.m_ResourcePool->GetTexture(DemoResourceIds::DepthBuffer),
                context.m_ResourcePool->GetTexture(DemoResourceIds::NrdNormalRoughness),
                context.m_ResourcePool->GetTexture(DemoResourceIds::NrdViewZ),
                context.m_ResourcePool->GetTexture(DemoResourceIds::NrdMotion),
                context.m_ResourcePool->GetTexture(DemoResourceIds::NrdDenoisedRadiance),
                context.m_ResourcePool->GetTexture(RenderGraph::ResourceIds::GRAPH_OUTPUT),
                context.m_Metadata.m_ScreenWidth,
                context.m_Metadata.m_ScreenHeight);
        });
}
