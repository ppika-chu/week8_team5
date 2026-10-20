#pragma once
#include "Buffer.h"
#include "Texture2D.h"
#include "RenderPacket.h"
#include "ShadowAtlas.h"
#include "ShadowCasterState.h"
#include "Core/Stats/ShadowStats.h"

class FRenderer;

// 맵 한 장의 LightVP
struct FShadowData
{
	FMatrix LightViewProj;
	float Depth = 0.0f;
	float Width = 0.0f;

	uint64 Revision = 0;
};

// Data delivered from FScene
// Light 한 개당 
struct FLightShadowData
{
	uint64 LightID = 0;			// UUID

	uint32 LightSlotIndex;		// Index for LightShadowRanges
								// Slot 0 is reserved for directional Light.
	TArray<FShadowData> Maps;	// Maps에 Cascade 수만큼 FShadowData 저장
	float DepthBias;
	uint32 Resolution = DefaultShadowResolution;
	float BlendWidth = 0.0f;		// Cascade 경계 Blend Width (UV)
	uint32 bDebugCascades = 0;		// Cascade Debug 용
	bool bAlwaysUpdate = false; 	// Shadow Map을 항상 업데이트할지 여부
};
 
// 맵 한 장의 Atlas 정보
struct alignas(16) FShadowInfo
{
	FMatrix LightViewProj;
	FVector2 AtlasOffset;
	FVector2 AtlasScale;

	float DepthBias;
	float PrescaledTangentTheta;
	uint32 ArrayIndex = 0;
	float FShadowInfo_padding;
};

// Editor가 Scene에 넘기는 Setting 값 -> 셰이더에서 읽기
struct FShadowSettings
{
	uint32 BaseResolution = DefaultShadowResolution;
	float  Distance     = 50.0f;
	uint32 NumCascades  = 1;
	float  SplitLambda  = 0.7f;
	float  BlendWidth   = 0.1f;
	bool   bDebugCascades = false;
};

struct alignas(16) FLightShadowRange
{
	uint32 Offset;
	uint32 Count;
	float BlendWidth;
	uint32 bDebugCascades;
};


struct FShadowKey
{
	uint64 LightID = 0;
	uint64 MapIndex = 0;

	bool operator==(const FShadowKey& Other) const
	{
		return LightID == Other.LightID && MapIndex == Other.MapIndex;
	}
};

struct FShadowKeyHash
{
	size_t operator()(const FShadowKey& Key) const
	{
		const size_t A = std::hash<uint64>{}(Key.LightID);
		const size_t B = std::hash<uint64>{}(Key.MapIndex);

		return A ^ (B + static_cast<size_t>(0x9e3779b9) + (A << 6) + (A >> 2));
	}
};

struct FShadowCacheEntry
{
	FShadowAtlasAllocation Allocation{};

	// 캐시된 깊이 데이터를 생성할 때 사용한 행렬.
	FMatrix RenderedLightViewProj;

	uint64 RenderedRevision = 0;
	// 현재 깊이 텍스처를 만들 때 참여한 Caster와 월드 변환 Revision.
	TArray<FShadowCasterState> RenderedCasterStates;
	bool bHasRenderedContent = false;
};

struct FShadowFrameEntry
{
	FShadowKey Key{};
	uint64 RequestedRevision = 0;
	// 이번 맵 생성에 필요한 Caster와 월드 변환 Revision.
	TArray<FShadowCasterState> RequestedCasterStates;
};

class FShadowRenderer
{
public:
	bool Init(FRenderer* InRenderer);

	void SetShadowCacheEnabled(bool bEnabled)
	{
		bEnableShadowMapCache = bEnabled;
	}

	void SetParameters(
		uint64 SceneID,
		TArray<FLightShadowData>&& Lights,
		uint32 LightSlotCount);
	void UpdateCasterSet(uint32 ShadowInfoIndex, const TArray<FShadowCasterState>& CasterStates);
	void BeginShadowPass();
	void OnRender(FRenderQueue& ShadowQueue, uint32 ShadowInfoIndex);
	void EndShadowPass();
	void BindForLighting() const;

	const TArray<FShadowInfo>& GetShadowInfos() const { return ShadowInfos; }
	const FShadowRenderStats& GetCurrentViewStats() const { return CurrentViewStats; }
	const TArray<FLightShadowRange>& GetLightShadowRanges() const { return LightShadowRanges; }

	//bool IsShadowEnabled() const { return ShadowData.bShadowEnabled != 0; }
	//const FMatrix& GetShadowViewProjection() const { return ShadowData.LightViewProj; }
	FTexture2D* GetShadowMap() const { return ShadowAtlas.GetTexture(); }
	const TArray<uint32>& GetDirtyShadowIndices() const { return DirtyShadowIndices; }

	TArray<FShadowAtlasAllocation> GetShadowMapRects(uint32 LightID) const;
	TArray<FShadowInfo> GetShadowInfosByLightUUID(uint32 LightID) const;

private:
	FRenderer* Renderer = nullptr;
	FShadowRenderStats CurrentViewStats{};
	uint64 ShadowPassStartCycles = 0;

	// Shadow
	//TUniquePtr<FConstantBuffer> ShadowCB{};
	FShadowAtlas ShadowAtlas{};
	//FShadowConstants ShadowData{};

	TUniquePtr<FStructuredBuffer> ShadowInfosBuffer{};
	// [Directional0, Local0, Local1, ... , LocalN]
	// Size = MaxDirectionalLights + MaxLocalLights
	TUniquePtr<FStructuredBuffer> LightShadowRangesBuffer{};

	TArray<FLightShadowRange> LightShadowRanges{};		
	TArray<FShadowInfo> ShadowInfos{};
	//TArray<FShadowAtlasAllocation> ShadowAllocations{}; // Use same index with ShadowInfos

	/* Cache */
	bool bEnableShadowMapCache = true;

	// 프레임간 유지
	TMap<FShadowKey, FShadowCacheEntry, FShadowKeyHash> ShadowCache;

	// SetParameters 마다 재구성: ShadowInfos와 같은 index
	TArray<FShadowFrameEntry> ShadowFrameEntries;

	// 이번에 그려야 하는 ShadowInfo index
	TArray<uint32> DirtyShadowIndices;

	uint64 CachedSceneID = 0;

	/* */
	const float HalfDirectionalLightSourceAngularDiameterRad = 0.0046748644f;
	const float DirectionalLightSourceTangentTheta = tan(HalfDirectionalLightSourceAngularDiameterRad);
};
