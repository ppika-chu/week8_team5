#include "EnginePCH.h"
#include "ShadowRenderer.h"

#include "Renderer.h"
#include "RenderCommand.h"
#include "RenderingInfo.h"

#include "Engine/Scene.h"

namespace
{
	// Camera Frustum에서 near, far 평면 꼭짓점 저장
	void GetFrustumPlaneCorners(const FMatrix& CameraViewProj, FVector OutNear[4], FVector OutFar[4])
	{
		const FMatrix InvVp = CameraViewProj.Inverse();
		for (int i = 0; i < 4; ++i)
		{
			const float x = (i & 1) ? 1.0f : -1.0f;
			const float y = (i & 2) ? 1.0f : -1.0f;
			const FVector4 N = InvVp.TransformFVector4(FVector4(x, y, 0.0f, 1.0f));
			const FVector4 F = InvVp.TransformFVector4(FVector4(x, y, 1.0f, 1.0f));

			OutNear[i] = FVector(N.X, N.Y, N.Z) / N.W; 
			OutFar[i] = FVector(F.X, F.Y, F.Z) / F.W;;
		}
	}

	// SliceNear, SliceFar 만큼 잘라서 꼭짓점 저장
	void GetSliceCorners(const FVector Near[4], const FVector Far[4], float CamNear, float CamFar, float SliceNear, float SliceFar, FVector OutCorners[8])
	{
		const float Range = CamFar - CamNear;
		const float tNear = (SliceNear - CamNear) / Range; 
		const float tFar = (SliceFar - CamNear) / Range;

		for (int i = 0; i < 4; ++i)
		{
			const FVector Edge = Far[i] - Near[i];		// i번 edge
			OutCorners[i] = Near[i] + Edge * tNear;		// near 꼭짓점
			OutCorners[i+4] = Near[i] + Edge * tFar;	// Far 꼭짓점
		}
	}
}

bool FShadowRenderer::Init(FRenderer* InRenderer)
{
	if (!InRenderer)
	{
		return false;
	}

	Renderer = InRenderer;

	ShadowCB = RenderCommand::CreateConstantBuffer(
		sizeof(FShadowConstants));

	if (!ShadowCB || !ShadowCB->GetBuffer())
	{
		return false;
	}

	D3D11_TEXTURE2D_DESC Desc{
		.Width = 2048,
		.Height = 2048,
		.MipLevels = 1,
		.ArraySize = 1,
		.Format = DXGI_FORMAT_R24G8_TYPELESS,
		.SampleDesc = {
			.Count = 1,
			.Quality = 0
		},
		.Usage = D3D11_USAGE_DEFAULT,
		.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE,
	};

	ShadowMap = RenderCommand::CreateTexture2D(Desc);
	
	if (!ShadowMap ||
		!ShadowMap->GetDSV() ||
		!ShadowMap->GetSRV())
	{
		return false;
	}

	SetParameters(FShadowConstants{});

	return true;
}

void FShadowRenderer::SetParameters(const FShadowConstants& Parameters)
{
	ShadowData = Parameters;
	RenderCommand::UpdateBufferData(
		ShadowCB.get(), 
		&Parameters, 
		sizeof(FShadowConstants));
}

void FShadowRenderer::BindForLighting() const
{
	constexpr auto Stages =
		EShaderBindFlagBits::Vertex |
		EShaderBindFlagBits::Pixel;

	RenderCommand::BindConstantBuffer(6, ShadowCB.get(), Stages);
	RenderCommand::BindShaderResource(13, ShadowMap.get(), Stages);
}

// Cascade N장은 같은 Resource 사용하므로 패스마다 unbind할 필요없음.
void FShadowRenderer::BeginShadowPasses()
{
	RenderCommand::UnbindShaderResource(
	13,
	EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
}

FMatrix FShadowRenderer::FitCascade(const FVector Corners[8], const FVector& F, const FVector& R, const FVector& U, const FBox& SceneBounds) const
{
	// cascade를 감싸는 구 만들기
	FVector Center(0.0f, 0.0f, 0.0f);
	for (int32 i = 0; i < 8; ++i) Center += Corners[i];
	Center /= 8.0f;

	float Radius = 0.0f;
	for (int32 i = 0; i < 8; ++i) Radius = FMath::Max(Radius, (Corners[i] - Center).Length());
	Radius = std::ceil(Radius * 16.0f) / 16.0f;		// 부동소수 오차 방지 ( 16은 그냥 임의 파라미터 )

	// 박스 폭, 텍셀 크기
	const float Size = Radius * 2.0f;
	const float Texel = Size / static_cast<float>(ShadowMap->GetWidth());

	// 텍셀 스냅 ( 반짝이는 그림자 방지 ) - Center를 텍셀 격차에 맞춰 옮기기
	const float CX = Center.Dot(R);		// Center가 Right 방향으로 어디있나
	const float CY = Center.Dot(U);		// Center가 Up 방향으로 어디있나
	Center += R * (std::floor(CX / Texel) * Texel - CX);
	Center += U * (std::floor(CY / Texel) * Texel - CY);

	// Near를 빛 쪽으로 당기기
	// Forward 성분이 양수인 축은 Min 쪽이, 음수인 축은 Max 쪽이 빛에 가까움
	const FVector Nearest(
		F.X >= 0 ? SceneBounds.Min.X : SceneBounds.Max.X,
		F.Y >= 0 ? SceneBounds.Min.Y : SceneBounds.Max.Y,
		F.Z >= 0 ? SceneBounds.Min.Z : SceneBounds.Max.Z);
	
	// Light Frustum의 Near, Far
	float TNear = std::min(-Radius, F.Dot(Nearest - Center));
	const float TFar = Radius;

	const FVector Eye = Center + F * TNear;
	const float Depth = TFar - TNear;

	// Light View * Light Projection
	return FMatrix::MakeView(Eye, F, R, U) * FMatrix::MakeOrthographic(Size, Size, 0.0f, Depth);

}

// Index 번 Cascade의 깊이를 rendering.
void FShadowRenderer::RenderCascade(uint32 Index, FRenderQueue& Queue)
{
	FRenderingInfo Target{
		.ViewportSetting = {
			.Width = ShadowMap->GetWidth(),
			.Height = ShadowMap->GetHeight()
		},
		.DepthStencil = {
			.Texture = ShadowMap.get(),
			.LoadOp = ERenderTargetLoadOp::Clear
		}
	};
	RenderDepthToTarget(Queue, GetCascadeViewProjection(Index), Target);
}

void FShadowRenderer::UpdateCascades(const FScene& Scene, const FMatrix& CameraViewProj, const FMatrix& CameraProj, float ShadowDistance)
{
	FShadowConstants Constants{};	// bShadowEnabled = 0
	const FDirectionalLightSceneProxy* Light = Scene.GetMainDirectionalLight();
	const FBox& Bounds = Scene.GetBounds();
	const bool bHasGeometry = (Bounds.Max - Bounds.Min).Length() > 0.0f;		// 빈 Scene이면 false

	if (Light && bHasGeometry)
	{
		// light axis
		FVector F, R, U;
		Light->GetShadowBasis(F, R, U);

		float CamNear, CamFar;
		bool bOrtho;
		ExtractDepthRange(CameraProj, CamNear, CamFar, bOrtho);
		const float SliceFar = FMath::Min(CamFar, CamNear + ShadowDistance);
		
		// Camera Frustum Near ~ SliceFar
		FVector Near[4], Far[4], Corners[8];
		GetFrustumPlaneCorners(CameraViewProj, Near, Far);
		GetSliceCorners(Near, Far, CamNear, CamFar, CamNear, SliceFar, Corners);

		Constants.LightViewProj = FitCascade(Corners, F, R, U, Bounds);		// Light Frustum의 VP 반환
		Constants.ShadowBias = Light->GetLightComponent()->GetShadowBias();
		Constants.bShadowEnabled = 1;		// 그림자 활성화

	}
	
	// b6 업로드
	SetParameters(Constants);

}

void FShadowRenderer::RenderDepthToTarget(FRenderQueue& Queue, const FMatrix& LightViewProj, const FRenderingInfo& Target)
{
	RenderCommand::BeginRenderPass(Target);
	Renderer->RenderQueueSorting(Queue, LightViewProj);
	RenderCommand::SetRasterizerState(ERasterizerState::ShadowDepth);
	RenderCommand::SetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	Renderer->RenderPreparedDepth(LightViewProj, true);
	RenderCommand::SetRasterizerState(ERasterizerState::SolidBack);
	RenderCommand::EndRenderPass(Target);

}