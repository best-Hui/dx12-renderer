#include <Passes/RaytracingDemoPasses.h>

#include <Passes/RaytracingDemoPassResources.h>
#include <Passes/SvgfPass.h>
#include <RaytracingDemo.h>

#include <RenderGraph/RenderPass.h>

std::unique_ptr<RenderGraph::RenderPass> RaytracingDemoPasses::Builder::CreateSvgfPass(RaytracingDemo& demo)
{
    using namespace RenderGraph;
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;

    return RenderPass::Create(
        L"SVGF Denoise",
        {
            { DemoResourceIds::NrdFinishedToken, InputType::Token },
            { DemoResourceIds::NrdNoisyRadiance, InputType::ShaderResource },
            { DemoResourceIds::GBufferNormal, InputType::ShaderResource },
            { DemoResourceIds::GBufferPosition, InputType::ShaderResource },
            { DemoResourceIds::GBufferAlbedoOcclusion, InputType::ShaderResource },
            { DemoResourceIds::GBufferEmissionMetallic, InputType::ShaderResource },
            { DemoResourceIds::DepthBuffer, InputType::ShaderResource },
        },
        {
            { RenderGraph::ResourceIds::GRAPH_OUTPUT, OutputType::UnorderedAccess },
            { DemoResourceIds::DenoiseFinishedToken, OutputType::Token },
        },
        [&demo](const RenderContext& context, CommandList& cmd)
        {
            if (demo.m_SvgfPass == nullptr)
            {
                return;
            }

            demo.m_SvgfPass->Execute(
                cmd,
                context.m_ResourcePool->GetTexture(DemoResourceIds::NrdNoisyRadiance),
                context.m_ResourcePool->GetTexture(DemoResourceIds::GBufferNormal),
                context.m_ResourcePool->GetTexture(DemoResourceIds::GBufferPosition),
                context.m_ResourcePool->GetTexture(DemoResourceIds::GBufferAlbedoOcclusion),
                context.m_ResourcePool->GetTexture(DemoResourceIds::GBufferEmissionMetallic),
                context.m_ResourcePool->GetTexture(DemoResourceIds::DepthBuffer),
                context.m_ResourcePool->GetTexture(RenderGraph::ResourceIds::GRAPH_OUTPUT),
                context.m_Metadata.m_ScreenWidth,
                context.m_Metadata.m_ScreenHeight);
        });
}
