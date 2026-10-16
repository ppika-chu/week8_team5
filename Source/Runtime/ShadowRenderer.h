#pragma once

#include "Render/Texture2D.h"
#include "Containers/Array.h"

class FScene;
class FPrimitiveSceneProxy;

class FShadowRenderer
{
public:
    bool Init(uint32 Resolution);
    void Render(const FMatrix& LightViewProjection, const FScene& Scene);

    FTexture2D* GetShadowMap() const;

private:
    void GatherShadowCandidates(const FScene& Scene);

private:
    TUniquePtr<FTexture2D> ShadowMap;

    FShaderProgram* ShadowShader = nullptr;
    TUniquePtr<FConstantBuffer> ShadowViewCB;
    TUniquePtr<FConstantBuffer> ShadowObjectCB;

    TArray<const FPrimitiveSceneProxy*> ShadowCandidates;
};

