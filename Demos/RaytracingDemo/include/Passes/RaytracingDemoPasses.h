#pragma once

#include <memory>
#include <vector>

namespace RenderGraph
{
    class RenderPass;
}

class RaytracingDemo;

namespace RaytracingDemoPasses
{
    class Builder
    {
    public:
        static std::unique_ptr<RenderGraph::RenderPass> CreateSetupPass(RaytracingDemo& demo);
        static std::unique_ptr<RenderGraph::RenderPass> CreateGBufferPass(RaytracingDemo& demo);
        static std::unique_ptr<RenderGraph::RenderPass> CreateSkyboxPass(RaytracingDemo& demo);
        static std::unique_ptr<RenderGraph::RenderPass> CreatePathTracingPass(RaytracingDemo& demo);
        static std::unique_ptr<RenderGraph::RenderPass> CreateNrdPass(RaytracingDemo& demo);
        static std::unique_ptr<RenderGraph::RenderPass> CreateSvgfPass(RaytracingDemo& demo);
        static std::unique_ptr<RenderGraph::RenderPass> CreateLightBillboardPass(RaytracingDemo& demo);
        static std::unique_ptr<RenderGraph::RenderPass> CreateImGuiPass(RaytracingDemo& demo);
    };
}
