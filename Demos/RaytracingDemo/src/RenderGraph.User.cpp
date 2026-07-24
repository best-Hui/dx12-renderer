#include "RenderGraph.User.h"

#include <Passes/RaytracingDemoPassResources.h>
#include <Passes/RaytracingDemoPasses.h>

#include <RaytracingDemo.h>
#include <RenderGraph/RenderPass.h>

std::unique_ptr<RenderGraph::RenderGraphRoot> RenderGraph::User::Create(RaytracingDemo& demo, CommandList&)
{
    std::vector<std::unique_ptr<RenderPass>> renderPasses;
    renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateSetupPass(demo));
    renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateBaseResourcesPass(demo));
    renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateSkyboxPass(demo));
    renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateDirectLightingPass(demo));
    renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateIndirectLightingPass(demo));
    renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateLightingCompositePass(demo));
    renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateDenoiserPreparePass(demo));
    renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateNrdPass(demo));
    renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateSvgfPass(demo));
    renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateLightBillboardPass(demo));
    renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateImGuiPass(demo));

    return std::make_unique<RenderGraphRoot>(
        std::move(renderPasses),
        RaytracingDemoRenderGraph::CreateTextureDescriptions(),
        RaytracingDemoRenderGraph::CreateBufferDescriptions(),
        RaytracingDemoRenderGraph::CreateTokenDescriptions());
}
