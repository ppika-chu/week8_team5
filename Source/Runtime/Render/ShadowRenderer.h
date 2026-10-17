#pragma once
#include "Buffer.h"
#include "Texture2D.h"
#include "RenderPacket.h"
#include "ShadowAtlas.h"

class FRenderer;

// Data delivered from FScene
struct FShadowData
{
	FMatrix LightViewProj;
	float DepthBias;
};

struct FLightShadowData
{
	uint32 LightIndex;			// Index for LightShadowRanges
	TArray<FShadowData> Maps;
};
 
struct alignas(16) FShadowInfo
{
	FMatrix LightViewProj;
	FVector2 AtlasOffset;
	FVector2 AtlasScale;

	float DepthBias;
	float Padding[3];
};

struct alignas(16) FLightShadowRange
{
	uint32 Offset;
	uint32 Count;
	uint32 Padding[2];
};

class FShadowRenderer
{
public:
	bool Init(FRenderer* InRenderer);

	void SetParameters(
		const TArray<FLightShadowData>& Lights,
		uint32 LightSlotCount);
	void BeginShadowPass();
	void OnRender(FRenderQueue& ShadowQueue, uint32 ShadowInfoIndex);
	void EndShadowPass();
	void BindForLighting() const;

	const TArray<FShadowInfo>& GetShadowInfos() const { return ShadowInfos; }
	const TArray<FLightShadowRange>& GetLightShadowRanges() const { return LightShadowRanges; }

	//bool IsShadowEnabled() const { return ShadowData.bShadowEnabled != 0; }
	//const FMatrix& GetShadowViewProjection() const { return ShadowData.LightViewProj; }
	FTexture2D* GetShadowMap() const { return ShadowAtlas.GetTexture(); }

private:
	FRenderer* Renderer = nullptr;

	// Shadow
	//TUniquePtr<FConstantBuffer> ShadowCB{};
	FShadowAtlas ShadowAtlas{};
	//FShadowConstants ShadowData{};

	TUniquePtr<FStructuredBuffer> ShadowInfosBuffer{};
	TUniquePtr<FStructuredBuffer> LightShadowRangesBuffer{};

	TArray<FLightShadowRange> LightShadowRanges{};
	TArray<FShadowInfo> ShadowInfos{};
	TArray<FShadowAtlasAllocation> ShadowAllocations{}; // Use same index with ShadowInfos
};