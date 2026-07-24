#include <Passes/RaytracingDemoPasses.h>

#include <Passes/RaytracingDemoPassResources.h>
#include <RaytracingDemo.h>

#include <DX12Library/CommandList.h>
#include <Framework/Mesh.h>
#include <Framework/ShaderResourceView.h>
#include <RenderGraph/RenderPass.h>

using namespace DirectX;

std::unique_ptr<RenderGraph::RenderPass> RaytracingDemoPasses::Builder::CreateGBufferPass(RaytracingDemo& demo)
{
    using namespace RenderGraph;
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;

    return RenderPass::Create(
        L"GBuffer",
        {
            { DemoResourceIds::SetupFinishedToken, InputType::Token }
        },
        {
            { DemoResourceIds::GBufferAlbedoOcclusion, OutputType::RenderTarget },
            { DemoResourceIds::GBufferSpecularSmoothness, OutputType::RenderTarget },
            { DemoResourceIds::GBufferNormal, OutputType::RenderTarget },
            { DemoResourceIds::GBufferEmissionMetallic, OutputType::RenderTarget },
            { DemoResourceIds::GBufferPosition, OutputType::RenderTarget },
            { DemoResourceIds::DepthBuffer, OutputType::DepthWrite },
            { DemoResourceIds::GBufferFinishedToken, OutputType::Token },
        },
        [&demo](const RenderContext&, CommandList& cmd)
        {
            demo.m_RootSignature->Bind(cmd);
            demo.m_GBufferShader->Bind(cmd);

            const XMMATRIX viewProjection = demo.m_Camera.GetViewMatrix() * demo.m_Camera.GetProjectionMatrix();
            for (const RaytracingDemo::SceneObject& object : demo.m_SceneObjects)
            {
                const RaytracingDemo::MaterialData& material = demo.m_Materials[object.MaterialIndex];

                RaytracingDemo::ModelConstants modelConstants{};
                modelConstants.Model = object.WorldMatrix;
                modelConstants.ModelViewProjection = object.WorldMatrix * viewProjection;
                modelConstants.InverseTransposeModel = XMMatrixTranspose(XMMatrixInverse(nullptr, object.WorldMatrix));
                demo.m_GBufferShader->SetConstantBuffer(cmd, "ModelCBuffer", modelConstants);

                RaytracingDemo::GBufferMaterialConstants materialConstants{};
                materialConstants.Diffuse = material.Diffuse;
                materialConstants.Specular = material.Specular;
                materialConstants.TilingOffset = material.TilingOffset;
                materialConstants.Metallic = material.Metallic;
                materialConstants.Roughness = material.Roughness;
                materialConstants.HasDiffuseMap = material.HasDiffuseMap;
                materialConstants.HasNormalMap = material.HasNormalMap;
                materialConstants.HasMetallicMap = material.HasMetallicMap;
                materialConstants.HasRoughnessMap = material.HasRoughnessMap;
                materialConstants.HasAmbientOcclusionMap = material.HasAmbientOcclusionMap;
                demo.m_GBufferShader->SetConstantBuffer(cmd, "MaterialCBuffer", materialConstants);
                demo.m_GBufferShader->SetTexture(cmd, "DiffuseTexture", ShaderResourceView(demo.m_Textures[material.DiffuseTextureIndex]));
                demo.m_GBufferShader->SetTexture(cmd, "NormalTexture", ShaderResourceView(demo.m_Textures[material.NormalTextureIndex]));
                demo.m_GBufferShader->SetTexture(cmd, "MetallicTexture", ShaderResourceView(demo.m_Textures[material.MetallicTextureIndex]));
                demo.m_GBufferShader->SetTexture(cmd, "RoughnessTexture", ShaderResourceView(demo.m_Textures[material.RoughnessTextureIndex]));
                demo.m_GBufferShader->SetTexture(cmd, "AmbientOcclusionTexture", ShaderResourceView(demo.m_Textures[material.AmbientOcclusionTextureIndex]));

                object.Model->Draw(cmd);
            }

            demo.m_GBufferShader->Unbind(cmd);
        });
}
