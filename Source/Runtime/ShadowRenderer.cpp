
#include "EnginePCH.h"

#include "Engine/Scene.h"
#include "Engine/SpotLightSceneProxy.h"

#include "ShadowRenderer.h"
#include "Render/RenderCommand.h"
#include "Render/RenderResourceManager.h"

#include "Components/StaticMeshComponent.h"

bool FShadowRenderer::Init(uint32 Resolution)
{
    if (Resolution == 0)
    {
        return false;
    }

    ShadowMapResolution = Resolution;
    ShadowMapArrayCapacity = START_SHADOW_MAP_CAPACITY;

    D3D11_TEXTURE2D_DESC ShadowMapDesc{};
    ShadowMapDesc.Width = Resolution;
    ShadowMapDesc.Height = Resolution;
    ShadowMapDesc.MipLevels = 1;
    ShadowMapDesc.ArraySize = ShadowMapArrayCapacity;
    ShadowMapDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
    ShadowMapDesc.SampleDesc.Count = 1;
    ShadowMapDesc.Usage = D3D11_USAGE_DEFAULT;
    ShadowMapDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

    ShadowMapArray = RenderCommand::CreateTexture2D(ShadowMapDesc);
    assert(ShadowMapArray != nullptr);
    assert(ShadowMapArray->GetDSV() != nullptr);
    assert(ShadowMapArray->GetSRV() != nullptr);

    ShadowShader = FRenderResourceManager::GetShaderProgram("Resources/Shader/ShadowDepth.hlsl");
    assert(ShadowShader != nullptr);
    assert(ShadowShader->VertexShader != nullptr);

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

FTexture2D* FShadowRenderer::GetShadowMapArray() const
{
    return ShadowMapArray.get();
}

const TArray<FSpotShadowData>& FShadowRenderer::GetSpotShadowDatas() const
{
    return SpotShadowDatas;
}

void FShadowRenderer::RenderPass(const FScene& Scene)
{
    SpotShadowDatas.Reset();
    assert(ShadowMapArray != nullptr);
    assert(ShadowShader != nullptr);
    assert(ShadowViewCB != nullptr);

    const uint32 ShadowMapCount = GatherSpotShadowDatas(Scene);
    if (ShadowMapCount == 0)
    {
        return;
    }

    constexpr uint32 BindResourceIndex = 13;
    RenderCommand::UnbindShaderResource(BindResourceIndex, EShaderBindFlagBits::Pixel);

    if (RecreateSpotShadowMapArray(ShadowMapCount) == false)
    {
        SpotShadowDatas.Reset();

        return;
    }

    GatherShadowCandidates(Scene);

    for (const FSpotShadowData& ShadowData : SpotShadowDatas)
    {
        if (ShadowData.ShadowMapSliceIndex < 0)
        {
            continue;
        }

        RenderShadowSlice(ShadowData);
    }
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

        const UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(PrimitiveSceneProxy->GetComponent());
        if (StaticMeshComponent && StaticMeshComponent->IsEditorIcon())
        {
            continue;
        }

        ShadowCandidates.Add(PrimitiveSceneProxy);
    }
}

uint32 FShadowRenderer::GatherSpotShadowDatas(const FScene& Scene)
{
    SpotShadowDatas.Reset();
    SpotShadowDatas.SetNum(Scene.LocalLightProxies.Num());

    uint32 ShadowMapCount = 0;
    for (uint32 LightIndex = 0; LightIndex < Scene.LocalLightProxies.Num(); ++LightIndex)
    {
        const FLocalLightSceneProxy* LightProxy = Scene.LocalLightProxies[LightIndex];
        if (!LightProxy || !LightProxy->IsVisible())
        {
            continue;
        }

        if (LightProxy->GetLightComponent()->GetLightType() != ELightType::Spot)
        {
            continue;
        }

        const auto* SpotLightProxy = static_cast<const FSpotLightSceneProxy*>(LightProxy);

        FSpotShadowData& ShadowData = SpotShadowDatas[LightIndex];
        ShadowData.ShadowLightViewProjection = SpotLightProxy->GetShadowViewMatrix() * SpotLightProxy->GetShadowProjectionMatrix();
        ShadowData.ShadowMapSliceIndex = static_cast<int32>(ShadowMapCount);
        
        ++ShadowMapCount;
    }

    return ShadowMapCount;
}

bool FShadowRenderer::RecreateSpotShadowMapArray(uint32 RequiredCount)
{
    if (RequiredCount == 0)
    {
        return true;
    }

    if (ShadowMapResolution == 0 || RequiredCount > D3D11_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION)
    {
        return false;
    }

    if (ShadowMapArray && RequiredCount <= ShadowMapArrayCapacity)
    {
        return true;
    }

    uint32 NewCapacity = std::max(START_SHADOW_MAP_CAPACITY, ShadowMapArrayCapacity);
    while (NewCapacity < RequiredCount)
    {
        NewCapacity *= 2;
    }

    D3D11_TEXTURE2D_DESC Desc{};
    Desc.Width = ShadowMapResolution;
    Desc.Height = ShadowMapResolution;
    Desc.MipLevels = 1;
    Desc.ArraySize = NewCapacity; // Make texture 2d slices
    Desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
    Desc.SampleDesc.Count = 1;
    Desc.Usage = D3D11_USAGE_DEFAULT;
    Desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

    TUniquePtr<FTexture2D> NewShadowMap = RenderCommand::CreateTexture2D(Desc);
    if (!NewShadowMap
        || !NewShadowMap->GetSRV()
        || !NewShadowMap->GetDSV())
    {
        return false;
    }

    ShadowMapArray = std::move(NewShadowMap);
    ShadowMapArrayCapacity = NewCapacity;

    return true;
}

void FShadowRenderer::RenderShadowSlice(const FSpotShadowData& ShadowData)
{
    FRenderingInfo PassInfo{};
    PassInfo.ViewportSetting.Width = ShadowMapArray->GetWidth();
    PassInfo.ViewportSetting.Height = ShadowMapArray->GetHeight();
    PassInfo.DepthStencil.Texture = ShadowMapArray.get();
    PassInfo.DepthStencilSliceIndex = ShadowData.ShadowMapSliceIndex;
    PassInfo.DepthStencil.LoadOp = ERenderTargetLoadOp::Clear;
    PassInfo.DepthStencil.ClearValue.DepthClearValue = 1.0f; // Initialize to deepest distance

    RenderCommand::BeginRenderPass(PassInfo);
    {
        FPipelineState PipelineState;
        PipelineState.Shader = ShadowShader;

        RenderCommand::BindPipelineState(PipelineState);
        RenderCommand::GetContext()->PSSetShader(nullptr, nullptr, 0);
        RenderCommand::UpdateBufferData(ShadowViewCB.get(), &ShadowData.ShadowLightViewProjection);
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
