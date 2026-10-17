#pragma once
#include "Buffer.h"
#include "Texture2D.h"
#include "RenderPacket.h"

class FRenderer;
struct alignas(16) FShadowConstants
{
	FMatrix LightViewProj;		// 64
	float ShadowBias;
	uint32 bShadowEnabled;
	float Padding[2];			// 16
};

static_assert(sizeof(FShadowConstants) == 80);

class FShadowRenderer
{
public:
	bool Init(FRenderer* InRenderer);

	void SetParameters(const FShadowConstants& Parameters);
	// void OnRender(FRenderQueue& ShadowQueue);
	void BindForLighting() const;

	bool IsShadowEnabled() const { return ShadowData.bShadowEnabled != 0; }
	const FMatrix& GetShadowViewProjection() const { return ShadowData.LightViewProj; }
	FTexture2D* GetShadowMap() const { return ShadowMap.get(); }

	void BeginShadowPasses();
	// Todo: CSM 구현되면 Cascade 수로 바꾸기
	uint32 GetNumCascades() const { return IsShadowEnabled()? 1 : 0; }
	const FMatrix& GetCascadeViewProjection(uint32 Index) const { return ShadowData.LightViewProj; }
	// Index 번 Cascade의 깊이를 rendering. 인자값 Queue는 GetCascadeViewProjection()으로 만들기.
	void RenderCascade(uint32 Index, FRenderQueue& Queue);
private:
	FRenderer* Renderer = nullptr;
	// Queue 하나를 Target DSV 깊이 그리기
	void RenderDepthToTarget(FRenderQueue& Queue, const FMatrix& LightViewProj, const FRenderingInfo& Target);

	// Shadow
	TUniquePtr<FConstantBuffer> ShadowCB{};
	TUniquePtr<FTexture2D> ShadowMap{};
	FShadowConstants ShadowData{};
};