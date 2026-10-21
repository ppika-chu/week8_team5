#include "EnginePCH.h"
#include "ShadowRenderer.h"

#include "Renderer.h"
#include "RenderCommand.h"
#include "RenderingInfo.h"
#include "Core/Windows/WindowsPlatformTime.h"

#include <algorithm>

constexpr uint32 MaxShadowInfos = 256;

constexpr uint32 MinShadowAtlasSize = 1024;
constexpr uint32 MaxShadowAtlasSize = 8192;

constexpr uint32 MaxShadowAtlasLayers = 16;

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

static bool AreShadowMatricesEqual(
	const FMatrix& Rendered,
	const FMatrix& Requested)
{
	// Compare exactly: FMatrix equality otherwise allows a tolerance.
	for (int32 Row = 0; Row < 4; ++Row)
	{
		for (int32 Column = 0; Column < 4; ++Column)
		{
			if (Rendered.M[Row][Column] != Requested.M[Row][Column])
			{
				return false;
			}
		}
	}

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
		LastShadowRequests.Reset();
		CachedSceneID = SceneID;
	}

	// Sort lights by final resolution (descending) to allocate larger shadow maps first
	std::sort(Lights.begin(), Lights.end(), [](const FLightShadowData& A, const FLightShadowData& B)
	{
		if (A.Resolution != B.Resolution)
		{
			return A.Resolution > B.Resolution;
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
			Light.Resolution == 0 ||
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

		const uint32 Resolution = Light.Resolution;

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
		bRetryShadowAllocation ||
		LastShadowRequests.Num() != Requests.Num();

	bRetryShadowAllocation = false;

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


	if (Requests.Num() == 0)
	{
		ShadowCache.Reset();
	}
	else if (bLayoutChanged)
	{
		TArray<FShadowAtlasAllocation> PackedAllocations;
		uint32 RequiredLayers = 0;

		// 이미 확보한 용량에 전체 요청이 들어가는지 먼저 확인한다.
		bool bAllPacked = FShadowAtlas::TryPack(
			Resolutions,
			ShadowAtlas.GetSize(),
			ShadowAtlas.GetLayerCount(),
			&PackedAllocations,
			&RequiredLayers);

		bool bCapacityReady = bAllPacked;

		if (!bAllPacked)
		{
			uint32 CandidateSize = FMath::Max(
				ShadowAtlas.GetSize(),
				MinShadowAtlasSize);

			// 한 장의 크기를 먼저 늘린다.
			while (CandidateSize <= MaxShadowAtlasSize)
			{
				bAllPacked = FShadowAtlas::TryPack(
					Resolutions,
					CandidateSize,
					1,
					&PackedAllocations,
					&RequiredLayers);

				if (bAllPacked || CandidateSize == MaxShadowAtlasSize)
				{
					break;
				}

				CandidateSize =
					CandidateSize > MaxShadowAtlasSize / 2
					? MaxShadowAtlasSize
					: CandidateSize * 2;
			}

			if (!bAllPacked && CandidateSize == MaxShadowAtlasSize)
			{
				bAllPacked = FShadowAtlas::TryPack(
					Resolutions,
					CandidateSize,
					MaxShadowAtlasLayers,
					&PackedAllocations,
					&RequiredLayers);
			}

			// 전체가 안 들어갔어도, 성공한 앞부분의 용량 확보를 시도한다.
			if (PackedAllocations.Num() > 0)
			{
				bCapacityReady = ShadowAtlas.EnsureTextureSize(
					CandidateSize,
					RequiredLayers);
			}

			if (!bCapacityReady)
			{
				// GPU 할당 실패 시 실제 보유한 용량으로 다시 계산한다.
				FShadowAtlas::TryPack(
					Resolutions,
					ShadowAtlas.GetSize(),
					ShadowAtlas.GetLayerCount(),
					&PackedAllocations,
					&RequiredLayers);
			}
		}

		uint32 PackedCount = PackedAllocations.Num();

		// 마지막 라이트가 일부만 들어갔다면 그 라이트 전체를 제외한다.
		if (PackedCount > 0 && PackedCount < Requests.Num())
		{
			const uint64 FirstRejectedLightID =
				Requests[PackedCount].Key.LightID;

			while (PackedCount > 0 &&
				Requests[PackedCount - 1].Key.LightID ==
				FirstRejectedLightID)
			{
				--PackedCount;
			}
		}

		// 간단한 구현: 재배치할 때 수용한 맵을 모두 다시 그린다.
		ShadowCache.Reset();

		for (uint32 i = 0; i < PackedCount; ++i)
		{
			FShadowCacheEntry Entry{};
			Entry.Allocation = PackedAllocations[i];
			Entry.RenderedRevision = 0;
			Entry.bHasRenderedContent = false;

			ShadowCache.Add(Requests[i].Key, std::move(Entry));
		}

		// 전체 요청을 기억해야 부분 수용 상태에서도 재패킹이 반복되지 않는다.
		LastShadowRequests.Reset(Requests.Num());

		for (const FShadowRequest& Request : Requests)
		{
			LastShadowRequests.Add(FShadowRequestSignature{
				.Key = Request.Key,
				.Resolution = Request.Resolution,
				});
		}
	}

	const bool bReady =
		ShadowAtlas.GetTexture() != nullptr &&
		ShadowAtlas.GetSize() > 0 &&
		ShadowAtlas.GetLayerCount() > 0;

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

			//assert(Cache);
			if (!Cache)
			{
				break;
			}

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
				.SlopeBias = Light.SlopeBias,
				.NearPlane = Data.NearPlane,
				.ArrayIndex = Allocation.ArrayIndex,
				});

			ShadowFrameEntries.Add(FShadowFrameEntry{
				.Key = Request.Key,
				.RequestedRevision = Data.Revision
				});

			const bool bNeedsRender =
				!bEnableShadowMapCache ||
				Light.bAlwaysUpdate ||
				!Cache->bHasRenderedContent ||
				Cache->RenderedRevision != Data.Revision ||
				!AreShadowMatricesEqual(
					Cache->RenderedLightViewProj,
					Data.LightViewProj);

			if (bNeedsRender)
			{
				DirtyShadowIndices.Add(ShadowInfoIndex);
			}

			++Range.Count;
		}
	}

	CurrentViewStats.AllocatedMapCount = static_cast<uint32>(ShadowInfos.Num());
	CurrentViewStats.AtlasSize = ShadowAtlas.GetSize();
	CurrentViewStats.AtlasLayerCount = ShadowAtlas.GetLayerCount();
	for (const auto& [Key, Cache] : ShadowCache)
	{
		const FShadowAtlasAllocation& Allocation = Cache.Allocation;
		CurrentViewStats.AllocatedTexelCount +=
			static_cast<uint64>(Allocation.Width) * Allocation.Height;
	}


	// Upload Count = 0 even if there is no request,
	// so that the last frame's shadow data is not used in the next frame.
	RenderCommand::UpdateBufferData(
		LightShadowRangesBuffer.get(),
		LightShadowRanges.GetData(),
		static_cast<uint32>(LightShadowRanges.size() * sizeof(FLightShadowRange)));
}

void FShadowRenderer::UploadShadowInfos()
{
	if (ShadowInfos.Num() == 0)
	{
		return;
	}

	RenderCommand::UpdateBufferData(
		ShadowInfosBuffer.get(),
		ShadowInfos.GetData(),
		static_cast<uint32>(ShadowInfos.Num() * sizeof(FShadowInfo)));
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

	RenderCommand::SetPrimitiveTopology(
		D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void FShadowRenderer::UpdateCasterSet(uint32 ShadowInfoIndex, const TArray<FShadowCasterState>& CasterStates)
{
	FShadowFrameEntry& Frame = ShadowFrameEntries[ShadowInfoIndex];
	Frame.RequestedCasterStates = CasterStates;
	auto& Requested = Frame.RequestedCasterStates;

	// 수집 순서가 달라도 같은 집합으로 비교한다.
	std::sort(Requested.begin(), Requested.end(), [](const auto& Left, const auto& Right)
	{
		return Left.CasterID < Right.CasterID;
	});

	FShadowInfo& ShadowInfo = ShadowInfos[ShadowInfoIndex];
	ShadowInfo.bShadowActive = 0;

	// 빈 맵은 Clear와 Draw를 생략하되 마지막 렌더 기록은 보관한다.
	if (Requested.Num() == 0)
	{
		const int32 DirtyListIndex = DirtyShadowIndices.Find(ShadowInfoIndex);
		if (DirtyListIndex != INDEX_NONE)
		{
			DirtyShadowIndices.RemoveAtSwap(static_cast<uint32>(DirtyListIndex));
		}
		return;
	}

	// 이미 Dirty여도 렌더링 후 저장할 이번 목록은 위에서 확보한다.
	if (DirtyShadowIndices.Find(ShadowInfoIndex) != INDEX_NONE)
	{
		return;
	}

	const FShadowCacheEntry* Cache = ShadowCache.Find(Frame.Key);
	assert(Cache);

	const auto& Rendered = Cache->RenderedCasterStates;
	const bool bSameCasters = std::equal(
		Rendered.begin(), Rendered.end(), Requested.begin(), Requested.end(),
		[](const FShadowCasterState& Previous, const FShadowCasterState& Current)
		{
			return Current.TransformRevision != 0 &&
				Previous.CasterID == Current.CasterID &&
				Previous.TransformRevision == Current.TransformRevision;
		});

	if (!bSameCasters)
	{
		DirtyShadowIndices.Add(ShadowInfoIndex);
		return;
	}

	ShadowInfo.bShadowActive = 1;
}

void FShadowRenderer::OnRender(FRenderQueue& ShadowQueue, uint32 ShadowInfoIndex)
{
	const FShadowFrameEntry& Frame = ShadowFrameEntries[ShadowInfoIndex];

	FShadowCacheEntry* Cache = ShadowCache.Find(Frame.Key);
	assert(Cache);

	FShadowInfo& ShadowInfo = ShadowInfos[ShadowInfoIndex];
	const FShadowAtlasAllocation& Tile = Cache->Allocation;

	// Clear 실패 시 이전 깊이를 샘플링하지 않도록 먼저 비활성화한다.
	ShadowInfo.bShadowActive = 0;

	// Invalidate cached content before attempting to update the tile.
	Cache->bHasRenderedContent = false;

	FTexture2D* AtlasTexture = ShadowAtlas.GetTexture();

	if (!AtlasTexture ||
		!AtlasTexture->GetSliceDSV(Tile.ArrayIndex))
	{
		return;
	}

	FRenderingInfo Pass{
		.ViewportSetting = {
			.StartX = static_cast<int32>(Tile.X),
			.StartY = static_cast<int32>(Tile.Y),
			.Width = Tile.Width,
			.Height = Tile.Height
		},
		.DepthStencil = {
			.Texture = AtlasTexture,
			.LoadOp = ERenderTargetLoadOp::Load,
			.ArrayIndex = Tile.ArrayIndex,
		},
	};

	RenderCommand::BeginRenderPass(Pass);

	// Clear the depth region for the current shadow tile
	if (!RenderCommand::ClearDepthRegion(
		AtlasTexture,
		Tile.X, Tile.Y,
		Tile.Width, Tile.Height,
		1.0f, Tile.ArrayIndex))
	{
		// Keep the cache invalid and leave the revision unchanged on failure.
		return;
	}

	const FLightShadowRange& DirectionalRange = LightShadowRanges[0];
	const bool bDirectional = ShadowInfoIndex >= DirectionalRange.Offset && ShadowInfoIndex < DirectionalRange.Offset + DirectionalRange.Count;
	RenderCommand::SetRasterizerState(bDirectional ? ERasterizerState::DirectionalShadowDepth : ERasterizerState::ShadowDepth);
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
	Cache->RenderedLightViewProj = ShadowInfo.LightViewProj;
	// 깊이�??�시 그린 ?�에�?캐시??Caster 기록??교체?�다.
	Cache->RenderedCasterStates = Frame.RequestedCasterStates;
	Cache->bHasRenderedContent = true;
	ShadowInfo.bShadowActive = 1;
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

TArray<FShadowAtlasAllocation> FShadowRenderer::GetShadowMapRects(uint32 LightID) const
{
	TArray<std::pair<uint64, FShadowAtlasAllocation>> Sorted;
	for (const auto& [Key, Cache] : ShadowCache)
	{
		if (Key.LightID == LightID)
		{
			Sorted.Add({ Key.MapIndex, Cache.Allocation });
		}
	}

	std::sort(Sorted.begin(), Sorted.end(), [](const std::pair<uint64, FShadowAtlasAllocation>& A, const std::pair<uint64, FShadowAtlasAllocation>& B) {
		if (A.second.Y == B.second.Y)
			return A.second.X < B.second.X;
		return A.second.Y < B.second.Y;
		});
	
	TArray<FShadowAtlasAllocation> Result;
	for (const auto& [Index, Allocation] : Sorted)
	{
		Result.Add(Allocation);
	}
	
	return Result;
}

TArray<FShadowInfo> FShadowRenderer::GetShadowInfosByLightUUID(uint32 LightID) const
{
	TArray<std::pair<uint64, FShadowInfo>> Sorted;
	for (uint32 i = 0; i < static_cast<uint32>(ShadowFrameEntries.Num()); ++i)
	{
		const FShadowFrameEntry& Frame = ShadowFrameEntries[i];
		if (Frame.Key.LightID == LightID)
		{
			Sorted.Add({ Frame.Key.MapIndex, ShadowInfos[i] });
		}
	}

	std::sort(Sorted.begin(), Sorted.end(), [](const std::pair<uint64, FShadowInfo>& A, const std::pair<uint64, FShadowInfo>& B) {
		return A.first < B.first;
		});

	TArray<FShadowInfo> Result;
	for (const auto& [Index, Info] : Sorted)
	{
		Result.Add(Info);
	}

	return Result;
}

uint64 FShadowRenderer::GetShadowAtlasAllocatedTexelCount() const
{
	uint64 AllocatedTexelCount = 0;

	for (const auto& Entry : ShadowCache)
	{
		const FShadowAtlasAllocation& Allocation = Entry.second.Allocation;

		AllocatedTexelCount +=
			static_cast<uint64>(Allocation.Width) * Allocation.Height;
	}

	return AllocatedTexelCount;
}
