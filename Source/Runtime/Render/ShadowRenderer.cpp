#include "EnginePCH.h"
#include "ShadowRenderer.h"

#include "Renderer.h"
#include "RenderCommand.h"
#include "RenderingInfo.h"

constexpr uint32 MaxShadowInfos = 64;

bool FShadowRenderer::Init(FRenderer* InRenderer)
{
	if (!InRenderer)
	{
		return false;
	}

	Renderer = InRenderer;

	ShadowInfosBuffer = RenderCommand::CreateDynamicStructuredBuffer(
		sizeof(FShadowInfo),
		MaxShadowInfos);

	if (!ShadowInfosBuffer || !ShadowInfosBuffer->GetSRV())
	{
		return false;
	}

	LightShadowRangesBuffer = RenderCommand::CreateDynamicStructuredBuffer(
		sizeof(FLightShadowRange),
		MaxLocalLights);

	if (!LightShadowRangesBuffer || !LightShadowRangesBuffer->GetSRV())
	{
		return false;
	}

	D3D11_TEXTURE2D_DESC Desc{
		.Width = 2048,
		.Height = 2048,
		.MipLevels = 1,
		.ArraySize = 1,
		.Format = DXGI_FORMAT_R24G8_TYPELESS,
		.SampleDesc = {
			.Count = 1,
			.Quality = 0
		},
		.Usage = D3D11_USAGE_DEFAULT,
		.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE,
	};

	ShadowMap = RenderCommand::CreateTexture2D(Desc);
	
	if (!ShadowMap ||
		!ShadowMap->GetDSV() ||
		!ShadowMap->GetSRV())
	{
		return false;
	}

	SetParameters({});

	return true;
}

void FShadowRenderer::SetParameters(const TArray<FShadowData>& InShadowData)
{
	ShadowInfos.Reset(MaxShadowInfos);
	LightShadowRanges.Reset(MaxLocalLights);

	for (const FShadowData& ShadowData : InShadowData)
	{
		// TODO: Make atlas


		FLightShadowRange LightShadowRange{
			.Offset = static_cast<uint32>(ShadowInfos.Num()),
			.Count = 1 // Directional Light only for now
		};

		FShadowInfo ShadowInfo{
			.LightViewProj = ShadowData.LightViewProj,
			.AtlasOffset = { 0, 0 },
			.AtlasScale = { 1, 1 },
			.DepthBias = ShadowData.DepthBias
		};

		LightShadowRanges.Add(LightShadowRange);
		ShadowInfos.Add(ShadowInfo);
	}

	// Make sure that there is at least one shadow info and light shadow range, even if they are empty
	if (LightShadowRanges.Num() == 0)
	{
		//ShadowInfos.Add(FShadowInfo{});
		LightShadowRanges.Add(FLightShadowRange{ .Offset = 0, .Count = 0 });
	}

	RenderCommand::UpdateBufferData(
		ShadowInfosBuffer.get(), 
		ShadowInfos.GetData(), 
		static_cast<uint32>(ShadowInfos.size() * sizeof(FShadowInfo)));

	RenderCommand::UpdateBufferData(
		LightShadowRangesBuffer.get(),
		LightShadowRanges.GetData(),
		static_cast<uint32>(LightShadowRanges.size() * sizeof(FLightShadowRange)));
}

void FShadowRenderer::BindForLighting() const
{
	constexpr auto Stages =
		EShaderBindFlagBits::Vertex |
		EShaderBindFlagBits::Pixel;

	//RenderCommand::BindConstantBuffer(6, ShadowCB.get(), Stages);
	RenderCommand::BindShaderResource(13, ShadowMap.get(), Stages);
	RenderCommand::BindShaderResource(14, ShadowInfosBuffer.get(), Stages);
	RenderCommand::BindShaderResource(15, LightShadowRangesBuffer.get(), Stages);
}

void FShadowRenderer::OnRender(FRenderQueue& ShadowQueue)
{
	// TEMP
	FShadowInfo& ShadowInfo = ShadowInfos[0];

	Renderer->RenderQueueSorting(
		ShadowQueue,
		ShadowInfo.LightViewProj);

	FRenderingInfo ShadowRenderingInfo{
		.ViewportSetting = {
			.Width = ShadowMap->GetWidth(),
			.Height = ShadowMap->GetHeight()
		},
		.DepthStencil = {
			.Texture = ShadowMap.get(),
			.LoadOp = ERenderTargetLoadOp::Clear
		}
	};

	RenderCommand::UnbindShaderResource(
		13,
		EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
	
	RenderCommand::BeginRenderPass(ShadowRenderingInfo);

	RenderCommand::SetRasterizerState(
		ERasterizerState::ShadowDepth);

	RenderCommand::SetPrimitiveTopology(
		D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	
	Renderer->RenderPreparedDepth(
		ShadowInfo.LightViewProj, true);

	RenderCommand::SetRasterizerState(
		ERasterizerState::SolidBack);

	RenderCommand::EndRenderPass(ShadowRenderingInfo);
}