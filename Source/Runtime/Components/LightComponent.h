#pragma once

#include "LightComponentBase.h"
#include "Render/LineBatcher.h"
#include "Math/EngineMath.h"

class ULightComponent : public ULightComponentBase
{
	DECLARE_CLASS(ULightComponent, ULightComponentBase)
	REFLECT_START(ULightComponent)
	PROPERTY_META(ShadowBias, .DragSpeed = 0.0001f, .Min = 0.0f, .Max = 0.05f, .Format = "%.4f")
	REFLECT_END()
public:
	ULightComponent();
	virtual ~ULightComponent() override;

	virtual ELightType GetLightType() const = 0;
	virtual void DrawDebug(FLineBatcher* LineBatcher) const;

	virtual class FLightSceneProxy* CreateSceneProxy() const
	{
		return NULL;
	}
	FLightSceneProxy* SceneProxy = nullptr;
	void SetShadowBias(float InBias) { ShadowBias = FMath::Max(0.0f, InBias); }
	float GetShadowBias() const  { return ShadowBias; }
protected:
	virtual bool ShouldCreateRenderState() const override
	{ return true; }

	virtual void CreateRenderState() override;
	virtual void DestroyRenderState() override;
private:
	float ShadowBias = 0.0f;

};