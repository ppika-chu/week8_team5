#include "EnginePCH.h"
#include "ShadowRenderer.h"

#include "Renderer.h"
#include "RenderCommand.h"
#include "RenderingInfo.h"

constexpr uint32 MaxShadowInfos = 64;

constexpr uint32 MinShadowAtlasSize = 1024;
constexpr uint32 MaxShadowAtlasSize = 16384;


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

	if (!ShadowAtlas.EnsureTextureSize(MinShadowAtlasSize))
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
	ShadowInfos.Reset(MaxShadowInfos);
	ShadowAllocations.Reset(MaxShadowInfos);

	const uint32 MaxLightSlots =
		MaxDirectionalLights + MaxLocalLights;

	const uint32 SlotCount = FMath::Clamp(LightSlotCount, 1u, MaxLightSlots);

	LightShadowRanges.Reset(SlotCount);

	// Ensure SlotCount <= size of range buffer
	for (uint32 i = 0; i < SlotCount; ++i)
	{
		LightShadowRanges.Add(FLightShadowRange{});
	}

	// Validate resolution scale and clamp to [0.1, 1.0]
	for (FLightShadowData& Light : Lights)
	{
		if (!std::isfinite(Light.ResolutionScale) ||
			Light.ResolutionScale <= 0.0f)
		{
			Light.ResolutionScale = 0.0f;
			continue;
		}


		Light.ResolutionScale =
			FMath::Clamp(Light.ResolutionScale, 0.1f, 1.0f);
	}

	// Sort lights by resolution scale (descending) to allocate larger shadow maps first
	std::sort(Lights.begin(), Lights.end(),
		[](const FLightShadowData& A, const FLightShadowData& B)
		{
			return A.ResolutionScale > B.ResolutionScale;
		});

	struct FShadowRequest
	{
		uint32 LightIndex;
		uint32 MapIndex;
	};

	TArray<FShadowRequest> Requests;
	TArray<uint32> Resolutions;

	TArray<uint32> AcceptedSlots;
	AcceptedSlots.Reset(SlotCount);

	for (uint32 i = 0; i < SlotCount; ++i)
	{
		AcceptedSlots.Add(0);
	}

	// 1. Confirm the shadow request
	for (uint32 LightIndex = 0;
		LightIndex < static_cast<uint32>(Lights.Num());
		++LightIndex)
	{
		const FLightShadowData& Light = Lights[LightIndex];

		if (Light.LightSlotIndex >= SlotCount ||
			Light.ResolutionScale <= 0.0f ||
			AcceptedSlots[Light.LightSlotIndex] != 0) // Check if the slot is already accepted
		{
			continue;
		}

		const uint32 MapCount = static_cast<uint32>(Light.Maps.Num());
		const uint32 RequestCount = static_cast<uint32>(Requests.Num());

		if (MapCount == 0 ||
			MapCount > MaxShadowInfos - RequestCount)
		{
			continue;
		}

		const uint32 Resolution = static_cast<uint32>(
			Light.ResolutionScale * DefaultShadowResolution);

		if (Resolution == 0)
		{
			continue;
		}

		AcceptedSlots[Light.LightSlotIndex] = 1;

		for (uint32 MapIndex = 0;
			MapIndex < MapCount;
			++MapIndex)
		{
			Requests.Add(FShadowRequest{
				.LightIndex = LightIndex,
				.MapIndex = MapIndex
				});

			Resolutions.Add(Resolution);
		}
	}

	// 2. Try to pack the shadow maps into the atlas
	TArray<FShadowAtlasAllocation> PackedAllocations;
	bool bReady = false;

	if (Requests.Num() > 0)
	{
		uint32 CandidateSize = FMath::Max(
			ShadowAtlas.GetSize(),
			MinShadowAtlasSize);

		while (CandidateSize <= MaxShadowAtlasSize)
		{
			if (FShadowAtlas::TryPack(
				Resolutions,
				CandidateSize,
				&PackedAllocations))
			{
				bReady = ShadowAtlas.EnsureTextureSize(CandidateSize);
				break;
			}

			// Prevent overflow and unnecessary iterations
			if (CandidateSize > MaxShadowAtlasSize / 2)
			{
				break;
			}

			CandidateSize *= 2;

		}
	}

	// 3. Update GPU data based on the packing results
	if (bReady)
	{
		const float InvAtlasSize =
			1.0f / static_cast<float>(ShadowAtlas.GetSize());

		for (uint32 RequestIndex = 0;
			RequestIndex < static_cast<uint32>(Requests.Num());
			++RequestIndex)
		{
			const FShadowRequest& Request = Requests[RequestIndex];
			const FLightShadowData& Light = Lights[Request.LightIndex];
			const FShadowData& Data = Light.Maps[Request.MapIndex];
			const FShadowAtlasAllocation& Allocation =
				PackedAllocations[RequestIndex];

			FLightShadowRange& Range =
				LightShadowRanges[Light.LightSlotIndex];

			if (Range.Count == 0)
			{
				Range.Offset =
					static_cast<uint32>(ShadowInfos.Num());

				Range.BlendWidth = Light.BlendWidth;
				Range.bDebugCascades = Light.bDebugCascades;
			}

			ShadowInfos.Add(FShadowInfo{
				.LightViewProj = Data.LightViewProj,
				.AtlasOffset = {
					Allocation.X * InvAtlasSize,
					Allocation.Y * InvAtlasSize
				},
				.AtlasScale = {
					Allocation.Width * InvAtlasSize,
					Allocation.Height * InvAtlasSize
				},
				.DepthBias = Light.DepthBias,
				.PrescaledTangentTheta = Data.Width > 0.0f ? DirectionalLightSourceTangentTheta * Data.Depth / Data.Width : 0.0f,
				});

			ShadowAllocations.Add(Allocation);
			++Range.Count;
		}
	}

	// If packing failed, ShadowInfos is empty, and Range.Count remains 0 for all lights.
	if (ShadowInfos.Num() > 0)
	{
		RenderCommand::UpdateBufferData(
			ShadowInfosBuffer.get(),
			ShadowInfos.GetData(),
			static_cast<uint32>(ShadowInfos.size() * sizeof(FShadowInfo)));
	}

	// Upload Count = 0 even if there is no request,
	// so that the last frame's shadow data is not used in the next frame.
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
	RenderCommand::BindSamplerState(1, ESamplerState::PointClamp, Stages);
	RenderCommand::BindSamplerState(2, ESamplerState::ComparisonLinearClamp, Stages);
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
