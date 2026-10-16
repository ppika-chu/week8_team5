#pragma once

#include "PointLightSceneProxy.h"

class FSpotLightSceneProxy : public FPointLightSceneProxy
{
public:
    using FPointLightSceneProxy::FPointLightSceneProxy;
    virtual void GetLightShaderParameters(FLightRenderParameters& Out) const override;

    // Todo: Shadow map
    FMatrix GetShadowViewMatrix() const;
    FMatrix GetShadowProjectionMatrix() const;
};