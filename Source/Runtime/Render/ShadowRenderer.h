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
	void OnRender(FRenderQueue& ShadowQueue);
	void BindForLighting() const;

	bool IsShadowEnabled() const { return ShadowData.bShadowEnabled != 0; }
	const FMatrix& GetShadowViewProjection() const { return ShadowData.LightViewProj; }
	FTexture2D* GetShadowMap() const { return ShadowMap.get(); }

private:
	FRenderer* Renderer = nullptr;

	// Shadow
	TUniquePtr<FConstantBuffer> ShadowCB{};
	TUniquePtr<FTexture2D> ShadowMap{};
	FShadowConstants ShadowData{};
};