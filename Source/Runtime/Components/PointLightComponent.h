#pragma once

#include "LocalLightComponent.h"

class UPointLightComponent : public ULocalLightComponent
{
	DECLARE_CLASS(UPointLightComponent, ULocalLightComponent)
	REFLECT_START(UPointLightComponent)
		PROPERTY(LightFalloffExponent)
		PROPERTY(SourceRadius)
	REFLECT_END()
public:
	UPointLightComponent();
	virtual ~UPointLightComponent() override;

	virtual FLightSceneProxy* CreateSceneProxy() const override;

	void DrawDebug(FLineBatcher* LineBatcher) const override;
	ELightType GetLightType() const override { return ELightType::Point; }

	float GetFalloff() const { return LightFalloffExponent; }
	void SetFalloff(float InFalloff) { LightFalloffExponent = InFalloff; }
	float GetSourceRadius() const { return SourceRadius; }
	void SetSourceRadius(float InRadius) { SourceRadius = FMath::Max(InRadius, 0.0f); }


protected:
	float LightFalloffExponent = 1.0f;
	float SourceRadius = 0.0f;
private:
	
};
