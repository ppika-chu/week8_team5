#pragma once
#include "Buffer.h"
#include "Texture2D.h"
#include "RenderPacket.h"
#include "RenderingInfo.h"

class FRenderer;
class FScene;

// 언리얼 default 3개, 최대 10개. 일단 4개 정도로 잡음.
constexpr uint32 MaxCascades = 4;

struct FCascadeData
{
	FMatrix LightViewProj;		// 64
	float Size;					// 박스 월드 폭
	float Depth;				// 박스 월드 깊이
	float Bias;					
	float Padding;				//16
};
static_assert(sizeof(FCascadeData) == 80);
struct alignas(16) FShadowConstants
{
	FCascadeData Cascades[MaxCascades];		// 80 * 4 = 320
	uint32 NumCascades;						// bShadowEnabled 대신 사용
	float LightSourceAngle;					// PCSS 계산을 위한 light 각도
	float CascadeBlendWidth;				// Blend 폭
	float Padding;						// 16
};

static_assert(sizeof(FShadowConstants) == 336);

class FShadowRenderer
{
public:
	bool Init(FRenderer* InRenderer);

	void SetParameters(const FShadowConstants& Parameters);
	// void OnRender(FRenderQueue& ShadowQueue);
	void BindForLighting() const;

	bool IsShadowEnabled() const { return ShadowData.NumCascades >  0; }
	FTexture2D* GetShadowMap() const { return ShadowMap.get(); }

	void BeginShadowPasses();
	uint32 GetNumCascades() const { return ShadowData.NumCascades; }
	const FMatrix& GetCascadeViewProjection(uint32 Index) const { return ShadowData.Cascades[Index].LightViewProj; }
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

	FCascadeData FitCascade(const FVector Corners[8], const FVector& F, const FVector& R, const FVector& U, const FBox& SceneBounds) const;
};