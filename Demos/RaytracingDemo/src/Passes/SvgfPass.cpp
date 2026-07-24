#include <Passes/SvgfPass.h>

#include <Passes/RaytracingDemoPassResources.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/Texture.h>
#include <Framework/CommonRootSignature.h>
#include <Framework/ComputeShader.h>
#include <Framework/RenderTexture.h>
#include <Framework/ShaderBlob.h>
#include <Framework/ShaderResourceView.h>
#include <Framework/UnorderedAccessView.h>

#include <algorithm>
#include <d3dx12.h>

namespace
{
    std::unique_ptr<ComputeShader> CreateReflectedComputeShader(const wchar_t* shaderName)
    {
        const ShaderBlob shader(shaderName);
        return std::make_unique<ComputeShader>(
            shader,
            ComputePipelineDescBuilder::ReflectedDefault(shader).Build());
    }
}

SvgfPass::SvgfPass(const std::shared_ptr<CommonRootSignature>& rootSignature)
    : m_RootSignature(rootSignature)
    , m_TemporalShader(CreateReflectedComputeShader(L"SvgfTemporal.cs.cso"))
    , m_AtrousShader(CreateReflectedComputeShader(L"SvgfAtrous.cs.cso"))
    , m_CompositeShader(CreateReflectedComputeShader(L"SvgfComposite.cs.cso"))
{
}

SvgfPass::~SvgfPass() = default;

void SvgfPass::ResetHistory()
{
    m_HistoryValid = false;
}

bool SvgfPass::EnsureCreated(const uint32_t width, const uint32_t height)
{
    if (m_Width == width && m_Height == height && m_TemporalColor != nullptr)
    {
        return true;
    }

    m_Width = width;
    m_Height = height;
    m_HistoryIndex = 0;
    m_HistoryValid = false;

    m_HistoryColor[0] = RenderTexture::CreateUav2D(DXGI_FORMAT_R32G32B32A32_FLOAT, width, height, L"SVGF History Color 0");
    m_HistoryColor[1] = RenderTexture::CreateUav2D(DXGI_FORMAT_R32G32B32A32_FLOAT, width, height, L"SVGF History Color 1");
    m_HistoryMoments[0] = RenderTexture::CreateUav2D(DXGI_FORMAT_R32G32_FLOAT, width, height, L"SVGF History Moments 0");
    m_HistoryMoments[1] = RenderTexture::CreateUav2D(DXGI_FORMAT_R32G32_FLOAT, width, height, L"SVGF History Moments 1");
    m_TemporalColor = RenderTexture::CreateUav2D(DXGI_FORMAT_R32G32B32A32_FLOAT, width, height, L"SVGF Temporal Color");
    m_TemporalMoments = RenderTexture::CreateUav2D(DXGI_FORMAT_R32G32_FLOAT, width, height, L"SVGF Temporal Moments");
    m_Variance = RenderTexture::CreateUav2D(DXGI_FORMAT_R32_FLOAT, width, height, L"SVGF Variance");
    m_AtrousPing = RenderTexture::CreateUav2D(DXGI_FORMAT_R32G32B32A32_FLOAT, width, height, L"SVGF Atrous Ping");
    m_AtrousPong = RenderTexture::CreateUav2D(DXGI_FORMAT_R32G32B32A32_FLOAT, width, height, L"SVGF Atrous Pong");
    return true;
}

void SvgfPass::Temporal(
    CommandList& commandList,
    const std::shared_ptr<Texture>& noisyRadiance,
    const std::shared_ptr<Texture>& gBufferNormal,
    const std::shared_ptr<Texture>& gBufferPosition,
    const std::shared_ptr<Texture>& motionVector,
    const std::shared_ptr<Texture>& depthTexture,
    const uint32_t width,
    const uint32_t height)
{
    const uint32_t previousIndex = m_HistoryIndex;
    const uint32_t nextIndex = 1u - m_HistoryIndex;

    TemporalConstants constants = {};
    constants.Width = width;
    constants.Height = height;
    constants.ResetHistory = m_HistoryValid ? 0u : 1u;
    constants.TemporalAlpha = std::clamp(m_Settings.TemporalAlpha, 0.001f, 1.0f);
    constants.MomentsAlpha = std::clamp(m_Settings.MomentsAlpha, 0.001f, 1.0f);
    constants.PhiNormal = m_Settings.PhiNormal;
    constants.PhiDepth = m_Settings.PhiDepth;

    m_TemporalShader->Bind(commandList);
    m_TemporalShader->SetConstantBuffer(commandList, "SvgfTemporalConstants", constants);
    commandList.SetTexture(*m_TemporalShader, "NoisyRadiance", ShaderResourceView(noisyRadiance));
    commandList.SetTexture(*m_TemporalShader, "GBufferNormal", ShaderResourceView(gBufferNormal));
    commandList.SetTexture(*m_TemporalShader, "GBufferPosition", ShaderResourceView(gBufferPosition));
    commandList.SetTexture(*m_TemporalShader, "DepthTexture", ShaderResourceView::DepthAsFloat(depthTexture));
    commandList.SetTexture(*m_TemporalShader, "MotionVector", ShaderResourceView(motionVector));
    commandList.SetTexture(*m_TemporalShader, "HistoryColor", ShaderResourceView(m_HistoryColor[previousIndex]));
    commandList.SetTexture(*m_TemporalShader, "HistoryMoments", ShaderResourceView(m_HistoryMoments[previousIndex]));
    m_TemporalShader->SetUnorderedAccessView(commandList, "TemporalColor", UnorderedAccessView(m_TemporalColor));
    m_TemporalShader->SetUnorderedAccessView(commandList, "TemporalMoments", UnorderedAccessView(m_TemporalMoments));
    m_TemporalShader->SetUnorderedAccessView(commandList, "Variance", UnorderedAccessView(m_Variance));
    m_TemporalShader->SetUnorderedAccessView(commandList, "OutHistoryColor", UnorderedAccessView(m_HistoryColor[nextIndex]));
    m_TemporalShader->SetUnorderedAccessView(commandList, "OutHistoryMoments", UnorderedAccessView(m_HistoryMoments[nextIndex]));
    commandList.Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1u);

    m_HistoryIndex = nextIndex;
    m_HistoryValid = true;
}

std::shared_ptr<Texture> SvgfPass::Atrous(
    CommandList& commandList,
    const std::shared_ptr<Texture>& gBufferNormal,
    const std::shared_ptr<Texture>& gBufferPosition,
    const std::shared_ptr<Texture>& depthTexture,
    const uint32_t width,
    const uint32_t height)
{
    std::shared_ptr<Texture> input = m_TemporalColor;
    std::shared_ptr<Texture> output = m_AtrousPing;
    const uint32_t iterationCount = std::clamp(m_Settings.AtrousIterations, 1u, 8u);

    for (uint32_t iteration = 0; iteration < iterationCount; ++iteration)
    {
        AtrousConstants constants = {};
        constants.Width = width;
        constants.Height = height;
        constants.StepSize = 1u << iteration;
        constants.PhiColor = m_Settings.PhiColor;
        constants.PhiNormal = m_Settings.PhiNormal;
        constants.PhiDepth = m_Settings.PhiDepth;

        output = (iteration % 2u) == 0u ? m_AtrousPing : m_AtrousPong;

        m_AtrousShader->Bind(commandList);
        m_AtrousShader->SetConstantBuffer(commandList, "SvgfAtrousConstants", constants);
        commandList.SetTexture(*m_AtrousShader, "InputColor", ShaderResourceView(input));
        commandList.SetTexture(*m_AtrousShader, "Variance", ShaderResourceView(m_Variance));
        commandList.SetTexture(*m_AtrousShader, "GBufferNormal", ShaderResourceView(gBufferNormal));
        commandList.SetTexture(*m_AtrousShader, "GBufferPosition", ShaderResourceView(gBufferPosition));
        commandList.SetTexture(*m_AtrousShader, "DepthTexture", ShaderResourceView::DepthAsFloat(depthTexture));
        m_AtrousShader->SetUnorderedAccessView(commandList, "OutputColor", UnorderedAccessView(output));
        commandList.Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1u);

        input = output;
    }

    return output;
}

void SvgfPass::Composite(
    CommandList& commandList,
    const std::shared_ptr<Texture>& input,
    const std::shared_ptr<Texture>& gBufferAlbedoOcclusion,
    const std::shared_ptr<Texture>& gBufferEmissionMetallic,
    const std::shared_ptr<Texture>& depthTexture,
    const std::shared_ptr<Texture>& output,
    const uint32_t width,
    const uint32_t height)
{
    CompositeConstants constants = {};
    constants.Width = width;
    constants.Height = height;

    m_CompositeShader->Bind(commandList);
    m_CompositeShader->SetConstantBuffer(commandList, "SvgfCompositeConstants", constants);
    commandList.SetTexture(*m_CompositeShader, "FilteredColor", ShaderResourceView(input));
    commandList.SetTexture(*m_CompositeShader, "DepthTexture", ShaderResourceView::DepthAsFloat(depthTexture));
    commandList.SetTexture(*m_CompositeShader, "GBufferAlbedoOcclusion", ShaderResourceView(gBufferAlbedoOcclusion));
    commandList.SetTexture(*m_CompositeShader, "GBufferEmissionMetallic", ShaderResourceView(gBufferEmissionMetallic));
    m_CompositeShader->SetUnorderedAccessView(commandList, "Output", UnorderedAccessView(output));
    commandList.Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1u);
}

void SvgfPass::Execute(
    CommandList& commandList,
    const std::shared_ptr<Texture>& noisyRadiance,
    const std::shared_ptr<Texture>& gBufferNormal,
    const std::shared_ptr<Texture>& gBufferPosition,
    const std::shared_ptr<Texture>& motionVector,
    const std::shared_ptr<Texture>& gBufferAlbedoOcclusion,
    const std::shared_ptr<Texture>& gBufferEmissionMetallic,
    const std::shared_ptr<Texture>& depthTexture,
    const std::shared_ptr<Texture>& output,
    const uint32_t width,
    const uint32_t height)
{
    if (!m_Enabled || !EnsureCreated(width, height))
    {
        return;
    }

    Temporal(commandList, noisyRadiance, gBufferNormal, gBufferPosition, motionVector, depthTexture, width, height);
    const std::shared_ptr<Texture> filtered = Atrous(commandList, gBufferNormal, gBufferPosition, depthTexture, width, height);
    Composite(commandList, filtered, gBufferAlbedoOcclusion, gBufferEmissionMetallic, depthTexture, output, width, height);
}
