#pragma once
#include "Math/Matrix.h"
#include "Math/Box.h"
#include "ShadowRenderer.h"

constexpr uint32 MaxCascades = 4;

// 카메라 Frustum의 near/far 평면 꼭짓점 4개
void GetFrustumPlaneCorners(const FMatrix& CameraViewProj, FVector OutNear[4], FVector OutFar[4]);

// [SliceNear, SliceFar] 구간 조각의 꼭짓점 8개
void GetSliceCorners(const FVector Near[4], const FVector Far[4], float CamNear, float CamFar,
                     float SliceNear, float SliceFar, FVector OutCorners[8]);

// 구간 조각 감싸는 라이트 Frustum 박스의 VP
// TileSize: 이 Cascade가 그려질 아틀라스 타일 한 변의 텍셀 수
FShadowData FitCascade(const FVector Corners[8], const FVector& F, const FVector& R, const FVector& U, uint32 TileSize);

// [Near, Far] 를 Count 개로 쪼개기 -> OutSplits[0] = Near, OutSplits[Count] = Far
void ComputeCascadeSplits(float Near, float Far, uint32 Count, float Lambda, float OutSplits[MaxCascades + 1]);