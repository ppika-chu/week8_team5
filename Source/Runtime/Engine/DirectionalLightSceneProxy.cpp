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

	/*
	const FVector Forward = Transform.GetForward();
    const FVector Right = Transform.GetRight();
    const FVector Up = Transform.GetUp();
	
	const FVector Eye = Center - Forward * ( Depth * 0.5f );
	
    LightView.M[0][0] = Forward.X; LightView.M[1][0] = Forward.Y; LightView.M[2][0] = Forward.Z; LightView.M[3][0] = -Forward.Dot(Eye);
    LightView.M[0][1] = Right.X; LightView.M[1][1] = Right.Y; LightView.M[2][1] = Right.Z; LightView.M[3][1] = -Right.Dot(Eye);
    LightView.M[0][2] = Up.X; LightView.M[1][2] = Up.Y; LightView.M[2][2] = Up.Z; LightView.M[3][2] = -Up.Dot(Eye);
    LightView.M[3][3] = 1.0f;
	
	// Light Projection 행렬
	FMatrix LightProjection;
	LightProjection = FMatrix::MakeOrthographic(Size, Size, 0, Depth);

	return LightView * LightProjection;
	*/
}
