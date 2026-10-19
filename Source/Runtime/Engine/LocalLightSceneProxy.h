#pragma once

#include "LightSceneProxy.h"

class FLocalLightSceneProxy : public FLightSceneProxy
{
public:
    // = FLocalLightSceneProxy(Param) : FLightSceneProxy(Param)
    using FLightSceneProxy::FLightSceneProxy;

    FMatrix GetShadowViewProjection(float VerticalFovDegrees, float NearZ, float FarZ) const;
};
