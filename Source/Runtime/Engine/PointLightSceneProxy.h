#pragma once

#include "LocalLightSceneProxy.h"

class FPointLightSceneProxy : public FLocalLightSceneProxy
{
public:
    static const auto& GetShadowFaces() { return ShadowFaces; }

    using FLocalLightSceneProxy::FLocalLightSceneProxy;
    void GetLightShaderParameters(FLightRenderParameters& Out) const override;

private:
	struct FShadowFace
	{
		FVector Direction;
		FVector Up;
	};

    inline static const FShadowFace ShadowFaces[6] = 
    {
        { {  1,  0,  0 }, {  0,  0,  1 } }, // +X
        { { -1,  0,  0 }, {  0,  0,  1 } }, // -X
        { {  0,  1,  0 }, {  0,  0,  1 } }, // +Y
        { {  0, -1,  0 }, {  0,  0,  1 } }, // -Y
        { {  0,  0,  1 }, { -1,  0,  0 } }, // +Z
        { {  0,  0, -1 }, {  1,  0,  0 } }, // -Z
    };
};