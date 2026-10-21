#pragma once

#include "LightComponentBase.h"
#include "Render/LineBatcher.h"
#include "Math/EngineMath.h"

class ULightComponent : public ULightComponentBase
{
	DECLARE_CLASS(ULightComponent, ULightComponentBase)
	REFLECT_START(ULightComponent)
		PROPERTY_META(ShadowResolutionScale, .DragSpeed = 0.01f, .Min = 0.1f, .Max = 1.0f, .Format = "%.2f")
		PROPERTY_META(ShadowBias, .DragSpeed = 0.01f, .Min = 0.0f, .Max = 10.0f, .Format = "%.2f")
		PROPERTY_META(ShadowSlopeBias, .DragSpeed = 0.01f, .Min = 0.0f, .Max = 10.0f, .Format = "%.2f")
		PROPERTY_META(ShadowSharpen, .DragSpeed = 0.0001f, .Min = 0.0f, .Max = 1.0f, .Format = "%.4f")
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

	/* Shadow */
	void SetShadowResolutionScale(float InScale) { ShadowResolutionScale = FMath::Clamp(InScale, 0.1f, 1.0f); }
	float GetShadowResolutionScale() const { return ShadowResolutionScale; }
	void SetShadowBias(float InBias) { ShadowBias = FMath::Max(0.0f, InBias); }
	float GetShadowBias() const  { return ShadowBias; }
	void SetShadowSlopeBias(float InSlopeBias) { ShadowSlopeBias = FMath::Max(0.0f, InSlopeBias); }
	float GetShadowSlopeBias() const { return ShadowSlopeBias; }
	void SetShadowSharpen(float InSharpen) { ShadowSharpen = FMath::Clamp(InSharpen, 0.0f, 1.0f); }
	float GetShadowSharpen() const { return ShadowSharpen; }
protected:
	virtual bool ShouldCreateRenderState() const override
	{ return true; }

	virtual void CreateRenderState() override;
	virtual void DestroyRenderState() override;
private:
	float ShadowResolutionScale = 1.0f;
	float ShadowBias = 0.05f;
	float ShadowSlopeBias = 1.0f;
	float ShadowSharpen = 0.0f;

};
