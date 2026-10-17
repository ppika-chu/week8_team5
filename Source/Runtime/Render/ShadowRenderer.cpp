#include "EnginePCH.h"
#include "ShadowRenderer.h"

#include "Renderer.h"
#include "RenderCommand.h"
#include "RenderingInfo.h"

bool FShadowRenderer::Init(FRenderer* InRenderer)
{
	if (!InRenderer)
	{
		return false;
	}

	Renderer = InRenderer;

	ShadowCB = RenderCommand::CreateConstantBuffer(
		sizeof(FShadowConstants));

	if (!ShadowCB || !ShadowCB->GetBuffer())
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

	SetParameters(FShadowConstants{});

	return true;
}

void FShadowRenderer::SetParameters(const FShadowConstants& Parameters)
{
	ShadowData = Parameters;
	RenderCommand::UpdateBufferData(
		ShadowCB.get(), 
		&Parameters, 
		sizeof(FShadowConstants));
}

void FShadowRenderer::BindForLighting() const
{
	constexpr auto Stages =
		EShaderBindFlagBits::Vertex |
		EShaderBindFlagBits::Pixel;

	RenderCommand::BindConstantBuffer(6, ShadowCB.get(), Stages);
	RenderCommand::BindShaderResource(13, ShadowMap.get(), Stages);
}

void FShadowRenderer::OnRender(FRenderQueue& ShadowQueue)
{
	if (!IsShadowEnabled())
	{
		return;
	}

	Renderer->RenderQueueSorting(
		ShadowQueue,
		ShadowData.LightViewProj);

	FRenderingInfo ShadowInfo{
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
	
	RenderCommand::BeginRenderPass(ShadowInfo);

	RenderCommand::SetRasterizerState(
		ERasterizerState::ShadowDepth);

	RenderCommand::SetPrimitiveTopology(
		D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	
	Renderer->RenderPreparedDepth(
		ShadowData.LightViewProj, true);

	RenderCommand::SetRasterizerState(
		ERasterizerState::SolidBack);

	RenderCommand::EndRenderPass(ShadowInfo);
}