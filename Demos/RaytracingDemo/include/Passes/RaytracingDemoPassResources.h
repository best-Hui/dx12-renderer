#pragma once

#include <DX12Library/Helpers.h>
#include <DX12Library/Texture.h>
#include <RenderGraph/ResourceDescription.h>

#include <d3d12.h>

#include <vector>

namespace RaytracingDemoRenderGraph
{
    constexpr FLOAT GBUFFER_CLEAR_COLOR[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    constexpr FLOAT OUTPUT_CLEAR_COLOR[] = { 0.4f, 0.6f, 0.9f, 1.0f };
    constexpr DXGI_FORMAT GBUFFER_COLOR_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
    constexpr DXGI_FORMAT GBUFFER_NORMAL_FORMAT = DXGI_FORMAT_R10G10B10A2_UNORM;
    constexpr DXGI_FORMAT GBUFFER_POSITION_FORMAT = DXGI_FORMAT_R32G32B32A32_FLOAT;
    constexpr DXGI_FORMAT ACCUMULATION_FORMAT = DXGI_FORMAT_R32G32B32A32_FLOAT;
    constexpr DXGI_FORMAT NRD_RADIANCE_FORMAT = DXGI_FORMAT_R32G32B32A32_FLOAT;
    constexpr DXGI_FORMAT NRD_NORMAL_ROUGHNESS_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
    constexpr DXGI_FORMAT NRD_VIEWZ_FORMAT = DXGI_FORMAT_R32_FLOAT;
    constexpr DXGI_FORMAT NRD_MOTION_FORMAT = DXGI_FORMAT_R16G16_FLOAT;
    constexpr DXGI_FORMAT DEPTH_FORMAT = DXGI_FORMAT_D32_FLOAT;
    constexpr DXGI_FORMAT OUTPUT_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;

    class ResourceIds
    {
    public:
        static inline const RenderGraph::ResourceId GBufferAlbedoOcclusion = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.GBufferAlbedoOcclusion");
        static inline const RenderGraph::ResourceId GBufferSpecularSmoothness = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.GBufferSpecularSmoothness");
        static inline const RenderGraph::ResourceId GBufferNormal = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.GBufferNormal");
        static inline const RenderGraph::ResourceId GBufferEmissionMetallic = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.GBufferEmissionMetallic");
        static inline const RenderGraph::ResourceId GBufferPosition = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.GBufferPosition");
        static inline const RenderGraph::ResourceId Accumulation = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.Accumulation");
        static inline const RenderGraph::ResourceId NrdNoisyRadiance = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.NrdNoisyRadiance");
        static inline const RenderGraph::ResourceId NrdDenoisedRadiance = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.NrdDenoisedRadiance");
        static inline const RenderGraph::ResourceId NrdNormalRoughness = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.NrdNormalRoughness");
        static inline const RenderGraph::ResourceId NrdViewZ = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.NrdViewZ");
        static inline const RenderGraph::ResourceId NrdMotion = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.NrdMotion");
        static inline const RenderGraph::ResourceId DepthBuffer = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.DepthBuffer");

        static inline const RenderGraph::ResourceId SetupFinishedToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.SetupFinished");
        static inline const RenderGraph::ResourceId GBufferFinishedToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.GBufferFinished");
        static inline const RenderGraph::ResourceId SkyboxFinishedToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.SkyboxFinished");
        static inline const RenderGraph::ResourceId RayTracingFinishedToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.RayTracingFinished");
        static inline const RenderGraph::ResourceId NrdFinishedToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.NrdFinished");
        static inline const RenderGraph::ResourceId DenoiseFinishedToken = RenderGraph::ResourceIds::GetResourceId(L"RaytracingDemo.DenoiseFinished");
    };

    std::vector<RenderGraph::TextureDescription> CreateTextureDescriptions();
    std::vector<RenderGraph::BufferDescription> CreateBufferDescriptions();
    std::vector<RenderGraph::TokenDescription> CreateTokenDescriptions();
}
