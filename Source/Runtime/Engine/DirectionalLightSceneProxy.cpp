#include "EnginePCH.h"
#include "DirectionalLightSceneProxy.h"
#include "Components/DirectionalLightComponent.h"

void FDirectionalLightSceneProxy::GetLightShaderParameters(FLightRenderParameters& Out) const
{
	const UDirectionalLightComponent* Dir = static_cast<const UDirectionalLightComponent*>(LightComponent);

	FTransform Transform;
	Transform.Rotation = Dir->GetWorldRotation();
	const FLinearColor Color(Dir->GetLightColor());

	Out.Type = static_cast<uint32>(ELightType::Directional);
	Out.Direction = Transform.GetForward();
	Out.Color = FVector(Color.R, Color.G, Color.B);
	Out.Intensity = Dir->GetIntensity();
}

// Light VP는 ShadowRenderer에서 만들기 (카메라를 아는 곳에서)
void FDirectionalLightSceneProxy::GetShadowBasis(FVector& OutForward, FVector& OutRight, FVector& OutUp) const
{
	const UDirectionalLightComponent* Dir = static_cast<const UDirectionalLightComponent*>(LightComponent);
	
	FTransform Transform;
	Transform.Rotation = Dir->GetWorldRotation();
	
	OutForward = Transform.GetForward();
    OutRight = Transform.GetRight();
    OutUp = Transform.GetUp();
}
