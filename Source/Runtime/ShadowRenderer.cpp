
#include "EnginePCH.h"

#include "Engine/Scene.h"
#include "ShadowRenderer.h"
#include "Render/RenderCommand.h"
#include "Render/RenderResourceManager.h"


FTexture2D* FShadowRenderer::GetShadowMap() const
{
    return ShadowMap.get();
}

bool FShadowRenderer::Init(uint32 Resolution)
{
    if (Resolution == 0)
    {
        return false;
    }

    D3D11_TEXTURE2D_DESC ShadowMapDesc{};
    ShadowMapDesc.Width = Resolution;
    ShadowMapDesc.Height = Resolution;
    ShadowMapDesc.MipLevels = 1;
    ShadowMapDesc.ArraySize = 1;
    ShadowMapDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
    ShadowMapDesc.SampleDesc.Count = 1;
    ShadowMapDesc.Usage = D3D11_USAGE_DEFAULT;
    ShadowMapDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

    ShadowMap = RenderCommand::CreateTexture2D(ShadowMapDesc);
    assert(ShadowMap != nullptr);
    assert(ShadowMap->GetDSV() != nullptr);
    assert(ShadowMap->GetSRV() != nullptr);

    ShadowShader = FRenderResourceManager::GetShaderProgram("Resources/Shader/ShadowDepth.hlsl");
    if (ShadowShader == nullptr || ShadowShader->VertexShader == nullptr)
    {
        return false;
    }

    /*
    const auto& Bindings = ShadowShader->VertexShader->GetBindings();
    if (Bindings.FindConstantBuffer("ShadowViewConstants") == nullptr)
    {
        return false;
    }
    */

    ShadowViewCB = RenderCommand::CreateConstantBuffer(sizeof(FMatrix));
    ShadowObjectCB = RenderCommand::CreateConstantBuffer(sizeof(FMatrix));
    /*
    if (ShadowViewCB == nullptr || ShadowViewCB->GetBuffer() == nullptr
        || ShadowObjectCB == nullptr || ShadowObjectCB->GetBuffer() == nullptr)
    {
        return false;
    }
    */

    return true;
}

void FShadowRenderer::Render(const FMatrix& LightViewProjection, const FScene& Scene)
{
    if (ShadowMap == nullptr || ShadowShader == nullptr || ShadowViewCB == nullptr)
    {
        return;
    }

    GatherShadowCandidates(Scene);

    const uint32 BindResourceIndex = 13;
    RenderCommand::UnbindShaderResource(BindResourceIndex, EShaderBindFlagBits::Pixel);

    FRenderingInfo PassInfo{};
    PassInfo.ViewportSetting.Width = ShadowMap->GetWidth();
    PassInfo.ViewportSetting.Height = ShadowMap->GetHeight();
    PassInfo.DepthStencil.Texture = ShadowMap.get();
    PassInfo.DepthStencil.LoadOp = ERenderTargetLoadOp::Clear;
    PassInfo.DepthStencil.ClearValue.DepthClearValue = 1.0f; // Initialize to deepest distance

    RenderCommand::BeginRenderPass(PassInfo);
    {
        FPipelineState PipelineState;
        PipelineState.Shader = ShadowShader;

        RenderCommand::BindPipelineState(PipelineState);
        RenderCommand::GetContext()->PSSetShader(nullptr, nullptr, 0);
        RenderCommand::UpdateBufferData(ShadowViewCB.get(), &LightViewProjection);
        RenderCommand::BindConstantBuffer(0, ShadowViewCB.get(), EShaderBindFlagBits::Vertex);
        RenderCommand::BindConstantBuffer(2, ShadowObjectCB.get(), EShaderBindFlagBits::Vertex);

        constexpr uint32 ShadowLODIndex = 0;

        for (const FPrimitiveSceneProxy* Proxy : ShadowCandidates)
        {
            RenderCommand::BindMesh(Proxy->GetMesh(), ShadowLODIndex);

            const FMatrix& World = Proxy->GetLocalToWorld();
            RenderCommand::UpdateBufferData(ShadowObjectCB.get(), &World);

            const FCachedMeshLOD& LOD = Proxy->GetLOD(ShadowLODIndex);
            for (uint32 SectionIndex = 0; SectionIndex < LOD.NumSections; ++SectionIndex)
            {
                const FCachedMeshSection& Section = Proxy->GetSection(LOD.FirstSection + SectionIndex);
                if (!Section.Material || Section.Material->BlendState != EBlendState::Opaque)
                {
                    continue;
                }

                RenderCommand::DrawIndexed(Section.IndexCount, Section.StartIndex);
            }
        }

    }
    RenderCommand::EndRenderPass(PassInfo);
}

void FShadowRenderer::GatherShadowCandidates(const FScene& Scene)
{
    ShadowCandidates.Reset();

    for (const FPrimitiveSceneProxy* PrimitiveSceneProxy : Scene.PrimitiveSceneProxies)
    {
        assert(PrimitiveSceneProxy != nullptr);

        if (PrimitiveSceneProxy->IsVisible() == false)
        {
            continue;
        }

        if (PrimitiveSceneProxy->GetMesh() == nullptr || PrimitiveSceneProxy->GetRenderState() == nullptr)
        {
            continue;
        }

        ShadowCandidates.Add(PrimitiveSceneProxy);
    }
}