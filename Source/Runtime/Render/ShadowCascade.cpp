#include "EnginePCH.h"
#include "ShadowCascade.h"

void GetFrustumPlaneCorners(const FMatrix& CameraViewProj, FVector OutNear[4], FVector OutFar[4])
{
	const FMatrix InvVP = CameraViewProj.Inverse();
	for (int32 i = 0; i < 4; ++i)
	{
		const float X = (i & 1) ? 1.0f : -1.0f;
		const float Y = (i & 2) ? 1.0f : -1.0f;

        // Near
		const FVector4 N = InvVP.TransformFVector4(FVector4(X, Y, 0.0f, 1.0f));
		// Far
        const FVector4 F = InvVP.TransformFVector4(FVector4(X, Y, 1.0f, 1.0f));
		OutNear[i] = FVector(N.X, N.Y, N.Z) / N.W;
		OutFar[i]  = FVector(F.X, F.Y, F.Z) / F.W;
	}
}

void GetSliceCorners(const FVector Near[4], const FVector Far[4], float CamNear, float CamFar, float SliceNear, float SliceFar, FVector OutCorners[8])
{
	// View 깊이가 선형이라 깊이 비율로 보간
	const float Range = CamFar - CamNear;
	const float T0 = (SliceNear - CamNear) / Range;
	const float T1 = (SliceFar  - CamNear) / Range;
	for (int32 i = 0; i < 4; ++i)
	{
		const FVector Edge = Far[i] - Near[i];
		OutCorners[i] = Near[i] + Edge * T0;        // Near 꼭짓점
		OutCorners[i + 4] = Near[i] + Edge * T1;    // Far 꼭짓점
	}
}

FMatrix FitCascade(const FVector Corners[8], const FVector& F, const FVector& R, const FVector& U, const FBox& SceneBounds, uint32 TileSize)
{
	// 감싸는 구 - 카메라가 회전해도 크기가 변하지 않음
    FVector Center(0.0f, 0.0f, 0.0f);
	for (int32 i = 0; i < 8; ++i) Center += Corners[i];
	Center /= 8.0f;

	float Radius = 0.0f;
	for (int32 i = 0; i < 8; ++i) Radius = FMath::Max(Radius, (Corners[i] - Center).Length());
	Radius = std::ceil(Radius * 16.0f) / 16.0f;     // 부동소수 오차로 반지름이 흔들리는 것 방지

	// 박스 폭과 텍셀 크기 ( 타일 한 장 기준 )
	const float Size  = Radius * 2.0f;
	const float Texel = Size / static_cast<float>(TileSize);

	// 텍셀 스냅 ( 반짝이는 그림자 방지 )
	const float CX = Center.Dot(R);
	const float CY = Center.Dot(U);
	Center += R * (std::floor(CX / Texel) * Texel - CX);
	Center += U * (std::floor(CY / Texel) * Texel - CY);

	// near/far를 씬 Bounds 양 끝에 고정
	const FVector Nearest(
		F.X >= 0 ? SceneBounds.Min.X : SceneBounds.Max.X,
		F.Y >= 0 ? SceneBounds.Min.Y : SceneBounds.Max.Y,
		F.Z >= 0 ? SceneBounds.Min.Z : SceneBounds.Max.Z);
	const FVector Farthest(
		F.X >= 0 ? SceneBounds.Max.X : SceneBounds.Min.X,
		F.Y >= 0 ? SceneBounds.Max.Y : SceneBounds.Min.Y,
		F.Z >= 0 ? SceneBounds.Max.Z : SceneBounds.Min.Z);

	const float TNear = FMath::Min(-Radius, F.Dot(Nearest  - Center));
	const float TFar = FMath::Max( Radius, F.Dot(Farthest - Center));

	const FVector Eye = Center + F * TNear;
	const float Depth = TFar - TNear;

	// Light View * LIght Projection
	return FMatrix::MakeView(Eye, F, R, U) * FMatrix::MakeOrthographic(Size, Size, 0.0f, Depth);
}