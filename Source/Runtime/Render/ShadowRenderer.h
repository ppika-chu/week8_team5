#pragma once
#include "Buffer.h"
#include "Texture2D.h"
#include "RenderPacket.h"

class FRenderer;

// Data delivered from FScene
struct FShadowData
{
	FMatrix LightViewProj;
	float DepthBias;
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

	void SetParameters(const TArray<FShadowData>& InShadowData);
	void OnRender(FRenderQueue& ShadowQueue);
	void BindForLighting() const;

	const TArray<FShadowInfo>& GetShadowInfos() const { return ShadowInfos; }
	const TArray<FLightShadowRange>& GetLightShadowRanges() const { return LightShadowRanges; }

	//bool IsShadowEnabled() const { return ShadowData.bShadowEnabled != 0; }
	//const FMatrix& GetShadowViewProjection() const { return ShadowData.LightViewProj; }
	FTexture2D* GetShadowMap() const { return ShadowMap.get(); }

private:
	FRenderer* Renderer = nullptr;

	// Shadow
	//TUniquePtr<FConstantBuffer> ShadowCB{};
	TUniquePtr<FTexture2D> ShadowMap{};
	//FShadowConstants ShadowData{};

	TUniquePtr<FStructuredBuffer> ShadowInfosBuffer{};
	TUniquePtr<FStructuredBuffer> LightShadowRangesBuffer{};

	TArray<FShadowInfo> ShadowInfos{};
	TArray<FLightShadowRange> LightShadowRanges{};
};