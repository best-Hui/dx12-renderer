#include <Passes/RaytracingDemoPassResources.h>

namespace RaytracingDemoRenderGraph
{
    std::vector<RenderGraph::TextureDescription> CreateTextureDescriptions()
    {
        const RenderGraph::RenderMetadataExpression<uint32_t> renderWidthExpression = [](const RenderGraph::RenderMetadata& metadata) { return metadata.m_ScreenWidth; };
        const RenderGraph::RenderMetadataExpression<uint32_t> renderHeightExpression = [](const RenderGraph::RenderMetadata& metadata) { return metadata.m_ScreenHeight; };

        return {
            { RenderGraph::ResourceIds::GRAPH_OUTPUT, renderWidthExpression, renderHeightExpression, OUTPUT_FORMAT, OUTPUT_CLEAR_COLOR, RenderGraph::ResourceInitAction::Clear },
            { ResourceIds::GBufferAlbedoOcclusion, renderWidthExpression, renderHeightExpression, GBUFFER_COLOR_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Clear },
            { ResourceIds::GBufferSpecularSmoothness, renderWidthExpression, renderHeightExpression, GBUFFER_COLOR_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Clear },
            { ResourceIds::GBufferNormal, renderWidthExpression, renderHeightExpression, GBUFFER_NORMAL_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Clear },
            { ResourceIds::GBufferEmissionMetallic, renderWidthExpression, renderHeightExpression, GBUFFER_COLOR_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Clear },
            { ResourceIds::GBufferPosition, renderWidthExpression, renderHeightExpression, GBUFFER_POSITION_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Clear },
            { ResourceIds::Accumulation, renderWidthExpression, renderHeightExpression, ACCUMULATION_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Discard },
            { ResourceIds::NrdNoisyRadiance, renderWidthExpression, renderHeightExpression, NRD_RADIANCE_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Discard },
            { ResourceIds::NrdDenoisedRadiance, renderWidthExpression, renderHeightExpression, NRD_RADIANCE_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Discard },
            { ResourceIds::NrdNormalRoughness, renderWidthExpression, renderHeightExpression, NRD_NORMAL_ROUGHNESS_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Discard },
            { ResourceIds::NrdViewZ, renderWidthExpression, renderHeightExpression, NRD_VIEWZ_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Discard },
            { ResourceIds::NrdMotion, renderWidthExpression, renderHeightExpression, NRD_MOTION_FORMAT, GBUFFER_CLEAR_COLOR, RenderGraph::ResourceInitAction::Discard },
            { ResourceIds::DepthBuffer, renderWidthExpression, renderHeightExpression, DEPTH_FORMAT, { 1.0f, 0u }, RenderGraph::ResourceInitAction::Clear },
        };
    }

    std::vector<RenderGraph::BufferDescription> CreateBufferDescriptions()
    {
        return {};
    }

    std::vector<RenderGraph::TokenDescription> CreateTokenDescriptions()
    {
        return {
            { ResourceIds::SetupFinishedToken },
            { ResourceIds::GBufferFinishedToken },
            { ResourceIds::SkyboxFinishedToken },
            { ResourceIds::RayTracingFinishedToken },
            { ResourceIds::NrdFinishedToken },
            { ResourceIds::DenoiseFinishedToken },
            { ResourceIds::LightBillboardFinishedToken },
        };
    }
}
