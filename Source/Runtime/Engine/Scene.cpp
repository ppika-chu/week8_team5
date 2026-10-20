#include "EnginePCH.h"
#include "Scene.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Math/BVH.h"
#include "PointLightSceneProxy.h"
#include "Render/ShadowCascade.h"

#include <cmath>

namespace
{
	uint32 CalculateShadowResolution(uint32 BaseResolution, float ResolutionScale)
	{
		if (!std::isfinite(ResolutionScale) || ResolutionScale <= 0.0f)
		{
			return 0;
		}

		const float ClampedScale = FMath::Clamp(ResolutionScale, 0.1f, 1.0f);

		return static_cast<uint32>(static_cast<float>(BaseResolution) * ClampedScale);
	}

	float GetPaddedShadowFov(
		float OriginalFovDegrees,
		uint32 Resolution,
		uint32 PaddingPixels = 4)
	{
		assert(Resolution > 2 * PaddingPixels);

		constexpr float DegreesToRadians = 3.14159265358979323846f / 180.0f;
		constexpr float RadiansToDegrees = 180.0f / 3.14159265358979323846f;

		// Scale = (Resolution / (Resolution - 2 * PaddingPixels))
		const float Scale =
			static_cast<float>(Resolution) /
			static_cast<float>(Resolution - 2 * PaddingPixels);

		const float HalfFovRadians =
			OriginalFovDegrees * 0.5f * DegreesToRadians;

		// Fov = 2 * atan(tan(HalfFov) * Scale)
		return 2.0f *
			std::atan(std::tan(HalfFovRadians) * Scale) * RadiansToDegrees;	
	}
}

void FScene::AddPrimitive(UPrimitiveComponent* Component)
{
	if (!Component || Component->SceneProxy) return;

	FPrimitiveSceneProxy* Proxy = new FPrimitiveSceneProxy(Component);
	Proxy->Scene = this;
	Proxy->PackedIndex = PrimitiveSceneProxies.Num();
	Component->SceneProxy = Proxy;
	MarkDirty(Proxy);
	MarkRenderStateDirty(Proxy);

	PrimitiveSceneProxies.Add(Proxy);
	PrimitiveBounds.Add(FAABB{});
	PrimitiveFlags.Add(0);

	bElementListChanged = true;
}

void FScene::RemovePrimitive(UPrimitiveComponent* Component)
{
	if (!Component || !Component->SceneProxy) return;

	FPrimitiveSceneProxy* Proxy = Component->SceneProxy;
	if (!Proxy) return;

	const uint32 Index = static_cast<uint32>(Proxy->PackedIndex);

	PrimitiveSceneProxies.RemoveAtSwap(Index);
	PrimitiveBounds.RemoveAtSwap(Index);
	PrimitiveFlags.RemoveAtSwap(Index);

	if (Index < static_cast<uint32>(PrimitiveSceneProxies.Num()))
		PrimitiveSceneProxies[Index]->PackedIndex = static_cast<int32>(Index);

	if (Proxy->bQueuedForUpdate)
	{
		const int32 Found = DirtyProxies.Find(Proxy);   // 프로젝트 TArray의 Find 이름에 맞게
		if (Found != INDEX_NONE) DirtyProxies.RemoveAtSwap(Found);
	}

	if (Proxy->bRenderStateQueued)
	{
		const int32 Found = RenderStateDirtyProxies.Find(Proxy);
		if (Found != INDEX_NONE) RenderStateDirtyProxies.RemoveAtSwap(Found);
	}

	Component->SceneProxy = nullptr;
	delete Proxy;

	bElementListChanged = true;
}

void FScene::RemoveAllPrimitives()
{
	// 하나씩 RemovePrimitive하면 대기열 검색(Find)이 물체 수만큼 반복되므로 통째로 비운다.
	for (FPrimitiveSceneProxy* Proxy : PrimitiveSceneProxies)
	{
		if (UPrimitiveComponent* Component = Proxy->GetComponent())
			Component->SceneProxy = nullptr;
		delete Proxy;
	}
	PrimitiveSceneProxies.Reset();
	PrimitiveBounds.Reset();
	PrimitiveFlags.Reset();
	DirtyProxies.Reset();
	RenderStateDirtyProxies.Reset();
	BVH.Clear();
	bElementListChanged = true;
}

void FScene::AddLight(ULightComponent* Component)
{
	if (!Component || Component->SceneProxy) return;

	FLightSceneProxy* Proxy = Component->CreateSceneProxy();
	if (!Proxy)
	{
		if (USkyLightComponent* Sky = Cast<USkyLightComponent>(Component))
			if (SkyLights.Find(Sky) == INDEX_NONE) SkyLights.Add(Sky);
		return;
	}

	Proxy->Scene = this;
	Component->SceneProxy = Proxy;

	if (Component->GetLightType() == ELightType::Directional)
	{
		Proxy->PackedIndex = DirectionalLightProxies.Num();
		DirectionalLightProxies.Add(static_cast<FDirectionalLightSceneProxy*>(Proxy));
	}
	else
	{
		Proxy->PackedIndex = LocalLightProxies.Num();
		LocalLightProxies.Add(static_cast<FLocalLightSceneProxy*>(Proxy));
		LocalLightParameters.Add(FLightRenderParameters{});
	}
}

void FScene::RemoveLight(ULightComponent* Component)
{
	if (!Component) return;

	FLightSceneProxy* Proxy = Component->SceneProxy;
	if (!Proxy)
	{
		if (USkyLightComponent* Sky = Cast<USkyLightComponent>(Component))
		{
			const int32 Found = SkyLights.Find(Sky);
			if (Found != INDEX_NONE) SkyLights.RemoveAtSwap(Found);
		}
		return;
	}

	const int32 Index = Proxy->PackedIndex;
	if (Component->GetLightType() == ELightType::Directional)
	{
		DirectionalLightProxies.RemoveAtSwap(Index);
		if (Index < DirectionalLightProxies.Num())
			DirectionalLightProxies[Index]->PackedIndex = Index;
	}
	else
	{
		LocalLightProxies.RemoveAtSwap(Index);
		LocalLightParameters.RemoveAtSwap(Index);      // 두 배열을 같이 움직여야 순서가 맞음
		if (Index < LocalLightProxies.Num())
			LocalLightProxies[Index]->PackedIndex = Index;
	}

	Component->SceneProxy = nullptr;
	delete Proxy;
}

void FScene::RemoveAllLights()
{
	for (FLightSceneProxy* Proxy : DirectionalLightProxies) delete Proxy;
	for (FLightSceneProxy* Proxy : LocalLightProxies)       delete Proxy;
	DirectionalLightProxies.Reset();
	LocalLightProxies.Reset();
	LocalLightParameters.Reset();
	SkyLights.Reset();
}

// 등록된 라이트를 셰이더용 상수로 모아 Renderer에 올린다. 컴포넌트 값을 매 프레임 직접 읽으므로 별도 갱신 경로가 필요 없다.
void FScene::UpdateLights(FRenderer* Renderer, FShadowRenderer* ShadowRenderer, const FMatrix& CameraViewProj, const FMatrix& CameraProj, const FShadowSettings& Shadow)
{
	if (!Renderer || !ShadowRenderer) return;
	
	FLightingConstants Constants{};
	Constants.Ambient.Color = FVector(1, 1, 1);
	Constants.Ambient.Intensity = 0.0f;
	
	for (USkyLightComponent* Sky : SkyLights)            // 기존 Sky 코드 그대로
	{
		if (!Sky->IsVisible()) continue;
		const FLinearColor Color(Sky->GetLightColor());
		Constants.Ambient.Color = FVector(Color.R, Color.G, Color.B);
		Constants.Ambient.Intensity = Sky->GetIntensity();
		break;
	}
	
	TArray<FLightShadowData> Lights;
	if (FDirectionalLightSceneProxy* Proxy = GetMainDirectionalLight())    // 셰이더는 하나만 받음
	{
		FLightRenderParameters P;
		Proxy->GetLightShaderParameters(P);
		Constants.Directional.Direction = P.Direction;
		Constants.Directional.Color = P.Color;
		Constants.Directional.Intensity = P.Intensity;

		// 씬 전체를 감싸는 Ortho 박스. 빈 Scene인 경우 Radius가 0 이므로 Shadow 계산 불가
		const FBox& Bounds = GetBounds();
		const float Radius = (Bounds.Max - Bounds.Min).Length() * 0.5f;
		const ULightComponent* LightComponent = Proxy->GetLightComponent();
		const uint32 Resolution = CalculateShadowResolution(Shadow.BaseResolution, LightComponent->GetShadowResolutionScale());
		if (Radius > 0.0f && LightComponent->IsCastingShadows() && Resolution > 0)
		{
			FVector Forward, Right, Up;
			Proxy->GetShadowBasis(Forward,Right, Up);

			// Camera near/far
			float CamNear, CamFar;
			bool bOrtho;
			ExtractDepthRange(CameraProj, CamNear, CamFar, bOrtho);
			
			// Shadow Distance 만큼 자르기
			const float SliceFar = FMath::Min(CamFar, CamNear + Shadow.Distance);

			// 카메라 조각 꼭짓점 8개
			FVector Near[4], Far[4];
			GetFrustumPlaneCorners(CameraViewProj, Near, Far);
			
			const uint32 Count = (bOrtho)? 1u : FMath::Clamp(Shadow.NumCascades, 1u, MaxCascades);
			float Splits[MaxCascades + 1];

			// Ortho 이면 Cascade 하나 만들고 Near, Far 조정
			if (bOrtho)
			{
				// 물체가 존재하는 곳 (씬 Bounds)의 최소 깊이, 최대 깊이 가져오기
				float MinZ = 1.0f, MaxZ = 0.0f;
				for (int32 c = 0; c < 8; ++c)
				{
					// 첫번째, 두번째, 세번째 비트 검사
					const FVector P((c & 1) ? Bounds.Max.X : Bounds.Min.X,
									(c & 2) ? Bounds.Max.Y : Bounds.Min.Y,
									(c & 4) ? Bounds.Max.Z : Bounds.Min.Z);
					
					const FVector4 Clip = CameraViewProj.TransformFVector4(FVector4(P.X, P.Y, P.Z, 1.0f));

					// MinZ, MaxZ 갱신
					MinZ = FMath::Min(MinZ, Clip.Z);
					MaxZ = FMath::Max(MaxZ, Clip.Z);
				}
				// 카메라 
				MinZ = FMath::Clamp(MinZ, 0.0f, 1.0f);
				MaxZ = FMath::Clamp(MaxZ, 0.0f, 1.0f);
				
				// Splits[0] ~ splits[1] 구간의 Cascade 하나
				Splits[0] = CamNear + MinZ * (CamFar - CamNear);
				Splits[1] = CamNear + MaxZ * (CamFar - CamNear);
			}
			else
			{
				ComputeCascadeSplits(CamNear, SliceFar, Count, Shadow.SplitLambda, Splits);
			}
			
			FLightShadowData Directional{};
			Directional.LightID = Proxy->GetLightComponent()->GetUUID();
			Directional.LightSlotIndex = 0;
			Directional.DepthBias = Proxy->GetLightComponent()->GetShadowBias();
			Directional.Resolution = Resolution;
			Directional.BlendWidth = Shadow.BlendWidth;
			Directional.bDebugCascades = Shadow.bDebugCascades ? 1u : 0u;
			
			// Cascade별 LightViewProj 비교로 갱신하므로 매번 Revision을 늘리지 않는다.
			// TODO: Caster 등 행렬 외 변경을 감지할 때 Revision을 늘린다.
			// Proxy->UpdateShadowRevision();
			for (uint32 i = 0; i < Count; ++i)
			{
				FVector Corners[8];
				GetSliceCorners(Near, Far, CamNear, CamFar, Splits[i], Splits[i+1], Corners);
				FShadowData ShadowData = FitCascade(Corners, Forward, Right, Up, Resolution);
				ShadowData.Revision = Proxy->GetShadowRevision();

				Directional.Maps.Add(ShadowData);
			}
			Lights.Add(Directional);
		}
	}

	for (FLocalLightSceneProxy* LightProxy : LocalLightProxies)
	{
		FLightRenderParameters& LightParameter = LocalLightParameters[LightProxy->PackedIndex];
		LightProxy->GetLightShaderParameters(LightParameter);
		if (!LightProxy->IsVisible())
			LightParameter.Radius = 0.0f;                              // 칸은 유지하고 영향만 없앤다

		const ULightComponent* LightComponent = LightProxy->GetLightComponent();
		const float NearZ = 0.1f;
		const float FarZ = LightParameter.Radius;
		const uint32 Resolution = CalculateShadowResolution(Shadow.BaseResolution, LightComponent->GetShadowResolutionScale());

		if (!LightProxy->IsVisible() || !LightComponent->IsCastingShadows() || FarZ <= NearZ || Resolution == 0)
		{
			continue;
		}

		FLightShadowData Local{};
		Local.LightID = LightComponent->GetUUID();
		Local.LightSlotIndex = MaxDirectionalLights + LightProxy->PackedIndex;
		Local.DepthBias = LightComponent->GetShadowBias();
		Local.Resolution = Resolution;

		// 맵별 LightViewProj 비교로 갱신하므로 매번 Revision을 늘리지 않는다.
		// TODO: Caster 등 행렬 외 변경을 감지할 때 Revision을 늘린다.
		// LightProxy->UpdateShadowRevision();
		switch (LightComponent->GetLightType())
		{
		case ELightType::Spot:
		{
			const auto* SpotLight = static_cast<const USpotLightComponent*>(LightComponent);
			const float OuterAngle = FMath::Clamp(SpotLight->GetOuterConeAngle(), 0.0f, 89.0f);

			if (OuterAngle <= 0.0f)
				continue;

			const float ShadowFov = GetPaddedShadowFov(OuterAngle * 2.0f, Resolution);

			FTransform LightTransform;
			LightTransform.Rotation = SpotLight->GetWorldRotation();

			FShadowData ShadowData{};
			ShadowData.LightViewProj = LightProxy->GetShadowViewProjection(LightTransform.GetForward(), LightTransform.GetUp(), ShadowFov, NearZ, FarZ);
			ShadowData.Revision = LightProxy->GetShadowRevision();

			Local.Maps.Add(ShadowData);

			break;
		}
		case ELightType::Point:
		{
			const float ShadowFov = GetPaddedShadowFov(90.0f, Resolution);

			for (const auto& Face : FPointLightSceneProxy::GetShadowFaces())
			{
				FShadowData ShadowData{};
				ShadowData.LightViewProj = LightProxy->GetShadowViewProjection(Face.Direction, Face.Up, ShadowFov, NearZ, FarZ);
				ShadowData.Revision = LightProxy->GetShadowRevision();

				Local.Maps.Add(ShadowData);
			}

			break;
		}
		default:
			// assert(false);
			break;
		}

		if (Local.Maps.Num() > 0)
		{
			Lights.Add(Local);
		}
	}

	Renderer->SetLighting(Constants, LocalLightParameters);
	ShadowRenderer->SetParameters(
		GetSceneID(),
		std::move(Lights),
		MaxDirectionalLights + MaxLocalLights);
}

FDirectionalLightSceneProxy* FScene::GetMainDirectionalLight()
{
	for (FDirectionalLightSceneProxy* Proxy : DirectionalLightProxies)
	{
		if (Proxy->IsVisible())	return Proxy;
	}
	return nullptr;
}


void FScene::AddFogInfo(uint32 Id, const FFogInfo& FogInfo)
{
	FExponentialHeightFogSceneInfo sceneFogInfo;
	sceneFogInfo.Id = Id;
	sceneFogInfo.FogInfo = FogInfo;


	FogInfos.Add(sceneFogInfo);
}

void FScene::RemoveFogInfo(uint32 Id)
{
	// 렌더러는 FogInfos[0]만 쓰므로, 순서를 유지하려고 RemoveAtSwap 대신 RemoveAt을 쓴다.
	for (int32 i = 0; i < FogInfos.Num(); ++i)
	{
		if (FogInfos[i].Id == Id)
		{
			FogInfos.RemoveAt(i);
			return;
		}
	}
}

void FScene::RemoveAllFogInfo()
{
	FogInfos.Reset();

}

void FScene::UpdateFogInfo(uint32 Id, const FFogInfo& FogInfo)
{
	for (FExponentialHeightFogSceneInfo& SceneFogInfo : FogInfos)
	{
		if (SceneFogInfo.Id == Id)
		{
			SceneFogInfo.FogInfo = FogInfo;
			return;
		}


	}
}

//void FScene::RemoveFogInfo(uint32 Id)
//{
//	for (int32 i = 0; i < FogInfos.Num(); ++i)
//	{
//		if (FogInfos[i].Id == Id)
//		{
//			FogInfos.RemoveAtSwap(i);
//			return;
//		}
//	}
//}


void FScene::UpdateAllTransforms()
{
	for (FPrimitiveSceneProxy* Proxy : RenderStateDirtyProxies)
	{
		Proxy->UpdateRenderState();
		Proxy->bRenderStateQueued = false;
	}
	RenderStateDirtyProxies.Reset();

	for (FPrimitiveSceneProxy* Proxy : DirtyProxies)
	{
		Proxy->UpdateTransform();
		PrimitiveBounds[Proxy->PackedIndex] = Proxy->GetBounds();
		PrimitiveFlags[Proxy->PackedIndex] = Proxy->GetComponent()->IsVisible() ? 1 : 0;
		Proxy->bQueuedForUpdate = false;
	}

	//const int32 Count = Proxies.Num();
	//for (int32 i = 0; i < Count; ++i)
	//{
	//	FPrimitiveSceneProxy* Proxy = Proxies[i];
	//	Proxy->UpdateTransform();

	//	const FAABB& Bounds = Proxy->GetBounds();
	//	if (PrimitiveBounds[i].Center != Bounds.Center || PrimitiveBounds[i].Extent != Bounds.Extent)
	//	{
	//		bBoundsChanged = true;
	//	}

	//	PrimitiveBounds[i] = Proxy->GetBounds();
	//	PrimitiveFlags[i] = Proxy->GetComponent()->IsVisible() ? 1 : 0;
	//}

	const bool bAnyMoved = DirtyProxies.Num() > 0;
	DirtyProxies.Reset();


	if (bElementListChanged)
	{
		BuildBVH();
		bElementListChanged = false;
	}
	else if (bAnyMoved)
	{
		BVH.Refit();
	}
}

void FScene::BuildBVH()
{
	BVH.Clear();
	// BillboardComponents.Reset();

	TArray<FPrimitiveSceneProxy*> Elements;
	Elements.Reserve(PrimitiveSceneProxies.Num());
	for (FPrimitiveSceneProxy* Proxy : PrimitiveSceneProxies)
	{
		if (Proxy && Proxy->GetComponent())
			Elements.Add(Proxy);
	}

	BVH.Build(std::span(Elements.GetData(), Elements.Num()));
}

void FScene::MarkDirty(FPrimitiveSceneProxy* Proxy)
{
	if (Proxy->bQueuedForUpdate) return;            // 중복 추가 방지
	Proxy->bQueuedForUpdate = true;
	DirtyProxies.Add(Proxy);
}

void FScene::MarkRenderStateDirty(FPrimitiveSceneProxy* Proxy)
{
	if (!Proxy || Proxy->bRenderStateQueued) return;
	Proxy->bRenderStateQueued = true;
	RenderStateDirtyProxies.Add(Proxy);
}
