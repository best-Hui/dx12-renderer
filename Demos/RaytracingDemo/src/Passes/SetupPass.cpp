#include <Passes/RaytracingDemoPasses.h>

#include <Passes/RaytracingDemoPassResources.h>
#include <RaytracingDemo.h>

#include <RenderGraph/RenderPass.h>

std::unique_ptr<RenderGraph::RenderPass> RaytracingDemoPasses::Builder::CreateSetupPass(RaytracingDemo& demo)
{
    using namespace RenderGraph;
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;

    return RenderPass::Create(
        L"Setup",
        {},
        {
            { DemoResourceIds::SetupFinishedToken, OutputType::Token }
        },
        [&demo](const RenderContext&, CommandList& cmd)
        {
            demo.m_RootSignature->Bind(cmd);
            demo.UploadSceneLightBuffers(cmd);
            demo.m_RootSignature->SetPipelineConstantBuffer(cmd, demo.BuildPipelineConstants());
        });
}
