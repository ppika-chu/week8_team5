#pragma once

#include "Render/Texture2D.h"
#include "Containers/Array.h"
#include "Core/Types.h"
#include "Math/Matrix.h"

struct alignas(16) FSpotShadowData
{
    FMatrix ShadowLightViewProjection = FMatrix::Identity;
    int32 ShadowMapSlice = -1;
    float ShadowBias = 0.001f;
    float Padding[2];
};

static_assert(sizeof(FSpotShadowData) == 80);

class FScene;
class FPrimitiveSceneProxy;

class FShadowRenderer
{
public:
    bool Init(uint32 Resolution);
    void Render(const FScene& Scene);

    FTexture2D* GetShadowMap() const;
    const TArray<FSpotShadowData>& GetSpotShadowDatas() const;

private:
    void GatherShadowCandidates(const FScene& Scene);
    uint32 GatherSpotShadowData(const FScene& Scene);
    bool EnsureShadowMapCapacity(uint32 RequiredCount);
    void RenderShadowSlice(const FSpotShadowData& ShadowData);

private:
    static constexpr uint32 START_CAPACITY = 8U;

    TUniquePtr<FTexture2D> ShadowMap;

    FShaderProgram* ShadowShader = nullptr;
    TUniquePtr<FConstantBuffer> ShadowViewCB;
    TUniquePtr<FConstantBuffer> ShadowObjectCB;

    TArray<const FPrimitiveSceneProxy*> ShadowCandidates;
    TArray<FSpotShadowData> SpotShadowDatas;

    uint32 ShadowMapResolution = 0;
    uint32 ShadowMapCapacity = 0;
};

