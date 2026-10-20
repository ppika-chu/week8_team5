#include "EnginePCH.h"
#include "ShadowRenderer.h"

#include "Renderer.h"
#include "RenderCommand.h"
#include "RenderingInfo.h"
#include "Core/Windows/WindowsPlatformTime.h"

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

	if (!RenderCommand::SupportsClearView())
	{
		// ClearView 지원하지 않으면 ShadowRenderer를 사용할 수 없다.
		return false;
	}

	SetParameters(0, {}, 0);

	return true;
}

void FShadowRenderer::SetParameters(
	uint64 SceneID,
	TArray<FLightShadowData>&& Lights,
	uint32 LightSlotCount)
{
	CurrentViewStats = {};
	for (const FLightShadowData& Light : Lights)
	{
		CurrentViewStats.RequestedMapCount += static_cast<uint32>(Light.Maps.Num());
	}

	ShadowInfos.Reset(MaxShadowInfos);
	ShadowFrameEntries.Reset(MaxShadowInfos);
	DirtyShadowIndices.Reset(MaxShadowInfos);

	const uint32 MaxLightSlots =
		MaxDirectionalLights + MaxLocalLights;

	const uint32 SlotCount = FMath::Clamp(LightSlotCount, 1u, MaxLightSlots);

	LightShadowRanges.Reset(SlotCount);

	// Ensure SlotCount <= size of range buffer
	for (uint32 i = 0; i < SlotCount; ++i)
	{
		LightShadowRanges.Add(FLightShadowRange{});
	}

	// Clear the shadow cache if the scene ID has changed
	if (CachedSceneID != SceneID)
	{
		ShadowCache.Reset();
		CachedSceneID = SceneID;
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
			if (A.ResolutionScale != B.ResolutionScale)
			{
				return A.ResolutionScale > B.ResolutionScale;
			}

			return A.LightID < B.LightID;
		});

	struct FShadowRequest
	{
		FShadowKey Key{};
		uint32 LightIndex;
		uint32 MapIndex;
		uint32 Resolution;
	};

	TArray<FShadowRequest> Requests;
	TArray<uint32> Resolutions;
	TArray<uint32> AcceptedSlots;
	TMap<uint64, bool> AcceptedLightIDs;

	Requests.Reset(MaxShadowInfos);
	Resolutions.Reset(MaxShadowInfos);
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
			AcceptedSlots[Light.LightSlotIndex] != 0 || // Check if the slot is already accepted
			AcceptedLightIDs.Contains(Light.LightID)) // Check if the light ID is already accepted
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
		AcceptedLightIDs.Add(Light.LightID, true);

		for (uint32 MapIndex = 0;
			MapIndex < MapCount;
			++MapIndex)
		{
			Requests.Add(FShadowRequest{
				.Key = {
					.LightID = Light.LightID,
					.MapIndex = MapIndex
				},
				.LightIndex = LightIndex,
				.MapIndex = MapIndex,
				.Resolution = Resolution
				});

			Resolutions.Add(Resolution);
		}
	}

	// 2. Check for layout changes
	// Re-build if the number, key, or resolution of shadow requests has changed
	bool bLayoutChanged =
		ShadowCache.Num() != static_cast<uint32>(Requests.Num());

	if (!bLayoutChanged)
	{
		for (const FShadowRequest& Request : Requests)
		{
			const FShadowCacheEntry* Cache =
				ShadowCache.Find(Request.Key);

			if (!Cache ||
				Cache->Allocation.Width != Request.Resolution ||
				Cache->Allocation.Height != Request.Resolution)
			{
				bLayoutChanged = true;
				break;
			}
		}
	}

	bool bReady = true;

	if (Requests.Num() == 0)
	{
		ShadowCache.Reset();
	}
	else if (bLayoutChanged) // Pack only if the layout has changed
	{
		// 3. Try to pack the shadow maps into the atlas
		TArray<FShadowAtlasAllocation> PackedAllocations;

		uint32 CandidateSize = FMath::Max(
			ShadowAtlas.GetSize(),
			MinShadowAtlasSize);

		bReady = false;

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

		// Replace the cache with new allocations if packing was successful
		if (bReady)
		{
			assert(PackedAllocations.Num() == Requests.Num());

			ShadowCache.Reset();

			for (uint32 i = 0;
				i < static_cast<uint32>(Requests.Num());
				++i)
			{
				ShadowCache.Add(
					Requests[i].Key,
					FShadowCacheEntry{
						.Allocation = PackedAllocations[i],
						.RenderedRevision = 0,
						.bHasRenderedContent = false
					});
			}
		}
	}

	// 4. Update GPU data and dirty lists based on the packing results
	if (bReady && Requests.Num() > 0)
	{
		const float InvAtlasSize =
			1.0f / static_cast<float>(ShadowAtlas.GetSize());

		//for (uint32 RequestIndex = 0;
		//	RequestIndex < static_cast<uint32>(Requests.Num());
		//	++RequestIndex)
		for (const FShadowRequest& Request : Requests)
		{
			const FLightShadowData& Light = Lights[Request.LightIndex];
			const FShadowData& Data = Light.Maps[Request.MapIndex];
			const FShadowCacheEntry* Cache =
				ShadowCache.Find(Request.Key);

			assert(Cache);

			const FShadowAtlasAllocation& Allocation =
				Cache->Allocation;

			const uint32 ShadowInfoIndex =
				static_cast<uint32>(ShadowInfos.Num());

			FLightShadowRange& Range =
				LightShadowRanges[Light.LightSlotIndex];

			if (Range.Count == 0)
			{
				Range.Offset = ShadowInfoIndex;
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

			ShadowFrameEntries.Add(FShadowFrameEntry{
				.Key = Request.Key,
				.RequestedRevision = Data.Revision
				});

			const bool bNeedsRender =
				Light.bAlwaysUpdate ||
				!Cache->bHasRenderedContent ||
				Cache->RenderedRevision != Data.Revision;

			if (bNeedsRender)
			{
				DirtyShadowIndices.Add(ShadowInfoIndex);
			}

			++Range.Count;
		}
	}

	CurrentViewStats.AllocatedMapCount = static_cast<uint32>(ShadowInfos.Num());
	CurrentViewStats.AtlasSize = ShadowAtlas.GetSize();
	for (const auto& [Key, Cache] : ShadowCache)
	{
		const FShadowAtlasAllocation& Allocation = Cache.Allocation;
		CurrentViewStats.AllocatedTexelCount +=
			static_cast<uint64>(Allocation.Width) * Allocation.Height;
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
	ShadowPassStartCycles = FPlatformTime::GetCycles64();

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
			.LoadOp = ERenderTargetLoadOp::Load, // LoadOp을 Load로 설정하여 기존 내용을 유지
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
	const FShadowFrameEntry& Frame = ShadowFrameEntries[ShadowInfoIndex];

	FShadowCacheEntry* Cache = ShadowCache.Find(Frame.Key);
	assert(Cache);

	const FShadowInfo& ShadowInfo = ShadowInfos[ShadowInfoIndex];
	const FShadowAtlasAllocation& Tile = Cache->Allocation;

	// Clear the depth region for the current shadow tile
	if (!RenderCommand::ClearDepthRegion(
		ShadowAtlas.GetTexture(),
		Tile.X, Tile.Y,
		Tile.Width, Tile.Height))
	{
		// If ClearDepthRegion fails, don't update the revision
		return;
	}
	Cache->bHasRenderedContent = false;

	RenderCommand::SetViewport(
		Tile.X, Tile.Y, Tile.Width, Tile.Height
	);

	Renderer->RenderQueueSorting(
		ShadowQueue,
		ShadowInfo.LightViewProj);

	const uint64 DrawCallsBefore = RenderCommand::GetDrawIndexedCallCount();
	Renderer->RenderPreparedDepth(
		ShadowInfo.LightViewProj, true);
	CurrentViewStats.DrawCallCount += RenderCommand::GetDrawIndexedCallCount() - DrawCallsBefore;
	++CurrentViewStats.UpdatedMapCount;

	Cache->RenderedRevision = Frame.RequestedRevision;
	Cache->bHasRenderedContent = true;
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

	const uint64 ElapsedCycles = FPlatformTime::GetCycles64() - ShadowPassStartCycles;
	CurrentViewStats.CpuTimeMs = FPlatformTime::ToMilliseconds64(ElapsedCycles);
}
