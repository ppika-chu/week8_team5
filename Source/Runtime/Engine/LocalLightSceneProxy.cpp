#include "EnginePCH.h"
#include "LocalLightSceneProxy.h"

/*
FMatrix FLocalLightSceneProxy::GetShadowViewProjection(
	float VerticalFovDegrees, 
	float NearZ, 
	float FarZ) const
{
	// Light View 행렬
	FMatrix LightView;
	FTransform Transform;
	Transform.Rotation = LightComponent->GetWorldRotation();

	const FVector Forward = Transform.GetForward();
	const FVector Right = Transform.GetRight();
	const FVector Up = Transform.GetUp();

	const FVector Eye = LightComponent->GetWorldLocation();

	LightView.M[0][0] = Forward.X; LightView.M[1][0] = Forward.Y; LightView.M[2][0] = Forward.Z; LightView.M[3][0] = -Forward.Dot(Eye);
	LightView.M[0][1] = Right.X;   LightView.M[1][1] = Right.Y;   LightView.M[2][1] = Right.Z;   LightView.M[3][1] = -Right.Dot(Eye);
	LightView.M[0][2] = Up.X;      LightView.M[1][2] = Up.Y;      LightView.M[2][2] = Up.Z;      LightView.M[3][2] = -Up.Dot(Eye);
	LightView.M[3][3] = 1.0f;

	// Light Projection 행렬
	const float HalfFov =
		FMath::DegreesToRadians(VerticalFovDegrees) * 0.5f;

	const float YScale = 1.0f / tan(HalfFov);
	const float XScale = YScale;
	const float ZScale = FarZ / (FarZ - NearZ);
	const float ZOffset = -NearZ * FarZ / (FarZ - NearZ);

	// 엔진 좌표계: X가 Forward
	const FMatrix LightProjection(
		0.0f, 0.0f, ZScale, 1.0f,
		XScale, 0.0f, 0.0f, 0.0f,
		0.0f, YScale, 0.0f, 0.0f,
		0.0f, 0.0f, ZOffset, 0.0f
	);

	return LightView * LightProjection;
}
*/

FMatrix FLocalLightSceneProxy::GetShadowViewProjection(const FVector& ViewDirection, const FVector& UpHint, float VerticalFovDegrees, float NearZ, float FarZ) const
{
	const FVector Forward = ViewDirection.GetSafeNormal();
	const FVector Right = UpHint.Cross(Forward).GetSafeNormal();
	// Todo: 다시 구하는 이유 알기
	const FVector Up = Forward.Cross(Right);
	const FVector Eye = LightComponent->GetWorldLocation();

	const FMatrix LightView(
		Forward.X, Right.X, Up.X, 0.0f,
		Forward.Y, Right.Y, Up.Y, 0.0f,
		Forward.Z, Right.Z, Up.Z, 0.0f,
		-Forward.Dot(Eye), -Right.Dot(Eye), -Up.Dot(Eye), 1.0f
	);

	const float HalfFov = FMath::DegreesToRadians(VerticalFovDegrees) * 0.5f;

	const float YScale = 1.0f / tan(HalfFov);
	const float XScale = YScale;
	const float ZScale = FarZ / (FarZ - NearZ);
	const float ZOffset = -NearZ * FarZ / (FarZ - NearZ);

	const FMatrix LightProjection(
		0.0f, 0.0f, ZScale, 1.0f,
		XScale, 0.0f, 0.0f, 0.0f,
		0.0f, YScale, 0.0f, 0.0f,
		0.0f, 0.0f, ZOffset, 0.0f
	);

	return LightView * LightProjection;
}