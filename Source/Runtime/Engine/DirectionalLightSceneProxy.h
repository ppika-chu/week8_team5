#pragma once

#include "LightSceneProxy.h"

class FDirectionalLightSceneProxy : public FLightSceneProxy
{
public:
	using FLightSceneProxy::FLightSceneProxy;
	void GetLightShaderParameters(FLightRenderParameters& Out) const override;
	FMatrix GetShadowViewProjection(FVector Center, float Depth, float Size) const;
};