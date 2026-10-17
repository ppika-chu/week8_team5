#pragma once
#include "Buffer.h"
#include "Texture2D.h"
#include "RenderPacket.h"
#include "RenderingInfo.h"

class FRenderer;
class FScene;
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
	// Index 번 Cascade의 깊이를 rendering. 인자값 Queue는 GetCascadeViewProjection()으로 만들기
	void RenderCascade(uint32 Index, FRenderQueue& Queue);
	// [ near, near + ShadowDistance ] -> Camera Depth 범위에 맞추어서 Cascade 만들고 cb 보내는 함수
	void UpdateCascades(const FScene& Scene, const FMatrix& CameraViewProj, const FMatrix& CameraProj, float ShadowDistance);

private:
	FRenderer* Renderer = nullptr;
	// Queue 하나를 Target DSV 깊이 그리기
	void RenderDepthToTarget(FRenderQueue& Queue, const FMatrix& LightViewProj, const FRenderingInfo& Target);

	// Shadow
	TUniquePtr<FConstantBuffer> ShadowCB{};
	TUniquePtr<FTexture2D> ShadowMap{};
	FShadowConstants ShadowData{};

	FMatrix FitCascade(const FVector Corners[8], const FVector& F, const FVector& R, const FVector& U, const FBox& SceneBounds) const;
};