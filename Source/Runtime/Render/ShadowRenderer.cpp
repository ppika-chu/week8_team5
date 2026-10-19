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
		MaxDirectionalLights + MaxLocalLights);

	if (!LightShadowRangesBuffer || !LightShadowRangesBuffer->GetSRV())
	{
		return false;
	}

	if (!ShadowAtlas.Init(16384))
	{
		return false;
	}

	SetParameters({}, 0);

	return true;
}

void FShadowRenderer::SetParameters(
	TArray<FLightShadowData>&& Lights,
	uint32 LightSlotCount)
{
	ShadowAtlas.ResetAllocations();

	ShadowInfos.Reset(MaxShadowInfos);
	ShadowAllocations.Reset(MaxShadowInfos);
	LightShadowRanges.Reset(LightSlotCount);

	const uint32 SlotCount = LightSlotCount > 0 ? LightSlotCount : 1;

	// Ensure SlotCount <= size of range buffer
	for (uint32 i = 0; i < SlotCount; ++i)
	{
		LightShadowRanges.Add(FLightShadowRange{});
	}

	// Sort lights by resolution scale (descending) to allocate larger shadow maps first
	std::sort(Lights.begin(), Lights.end(),
		[](const FLightShadowData& A, const FLightShadowData& B)
		{
			return A.ResolutionScale > B.ResolutionScale;
		});

	for (const FLightShadowData& Light : Lights)
	{
		if (Light.LightSlotIndex >= SlotCount)
			continue;

		const uint32 MapCount = static_cast<uint32>(Light.Maps.Num());

		// Skip if no shadow maps or shadow info buffer
		if (MapCount == 0 ||
			static_cast<uint32>(ShadowInfos.Num()) + MapCount > MaxShadowInfos)
		{
			continue;
		}

		FLightShadowRange Range{
			.Offset = static_cast<uint32>(ShadowInfos.Num()),
			.BlendWidth = Light.BlendWidth,
			.bDebugCascades = Light.bDebugCascades,
		};

		const float DepthBias = Light.DepthBias;
		const uint32 Resolution = static_cast<uint32>(Light.ResolutionScale * DefaultShadowResolution);
		for (const FShadowData& Data : Light.Maps)
		{
			FShadowAtlasAllocation Allocation{};
			if (!ShadowAtlas.Allocate(Resolution, &Allocation))
			{
				break;
			}

			const float InvAtlasSize =
				1.0f
				/ static_cast<float>(ShadowAtlas.GetSize());

			FShadowInfo ShadowInfo{
				.LightViewProj = Data.LightViewProj,
				.AtlasOffset = {
					Allocation.X * InvAtlasSize,
					Allocation.Y * InvAtlasSize
				},
				.AtlasScale = {
					Allocation.Width * InvAtlasSize,
					Allocation.Height * InvAtlasSize
				},
				.DepthBias = DepthBias
			};

			ShadowInfos.Add(ShadowInfo);
			ShadowAllocations.Add(Allocation);
		};

		Range.Count = static_cast<uint32>(
			ShadowInfos.Num() - Range.Offset);

		LightShadowRanges[Light.LightSlotIndex] = Range;
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
	RenderCommand::BindShaderResource(13, ShadowAtlas.GetTexture(), Stages);
	RenderCommand::BindShaderResource(14, ShadowInfosBuffer.get(), Stages);
	RenderCommand::BindShaderResource(15, LightShadowRangesBuffer.get(), Stages);
}

void FShadowRenderer::BeginShadowPass()
{
	constexpr auto Stages =
		EShaderBindFlagBits::Vertex |
		EShaderBindFlagBits::Pixel;

	RenderCommand::UnbindShaderResource(13, Stages);

	FRenderingInfo Pass{
		.ViewportSetting = {
			.Width = ShadowAtlas.GetSize(),
			.Height = ShadowAtlas.GetSize(),
		},
		.DepthStencil = {
			.Texture = ShadowAtlas.GetTexture(),
			.LoadOp = ERenderTargetLoadOp::Clear
		}
	};

	RenderCommand::BeginRenderPass(Pass);

	RenderCommand::SetRasterizerState(
		ERasterizerState::ShadowDepth);
	RenderCommand::SetPrimitiveTopology(
		D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void FShadowRenderer::OnRender(FRenderQueue& ShadowQueue, uint32 ShadowInfoIndex)
{
	const FShadowInfo& ShadowInfo = ShadowInfos[ShadowInfoIndex];
	const FShadowAtlasAllocation& Tile =
		ShadowAllocations[ShadowInfoIndex];

	RenderCommand::SetViewport(
		Tile.X, Tile.Y, Tile.Width, Tile.Height
	);

	Renderer->RenderQueueSorting(
		ShadowQueue,
		ShadowInfo.LightViewProj);

	Renderer->RenderPreparedDepth(
		ShadowInfo.LightViewProj, true);
}

void FShadowRenderer::EndShadowPass()
{
	FRenderingInfo Pass{
		.DepthStencil = {
			.Texture = ShadowAtlas.GetTexture()
		}
	};

	RenderCommand::EndRenderPass(Pass);

	RenderCommand::SetRasterizerState(
		ERasterizerState::SolidBack);
}