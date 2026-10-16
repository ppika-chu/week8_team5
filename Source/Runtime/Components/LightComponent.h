#pragma once

#include "LightComponentBase.h"
#include "Render/LineBatcher.h"

class ULightComponent : public ULightComponentBase
{
	DECLARE_CLASS(ULightComponent, ULightComponentBase)
	REFLECT_START(ULightComponent)
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

	/* Getter */
	float GetShadowResoulutionScale() const { return ShadowResoulutionScale; }
	float GetShadowBias() const { return ShadowBias; }
	float GetShadowSlopeBias() const { return ShadowSlopeBias; }
	float GetShadowSharpen() const { return ShadowSharpen; }
	float GetShadowAngle() const { return ShadowAngle; }
	float GetShadowNearPlane() const { return ShadowNearPlane; }
	float GetShadowFarPlane() const { return ShadowFarPlane; }

protected:
	virtual bool ShouldCreateRenderState() const override
	{ return true; }

	virtual void CreateRenderState() override;
	virtual void DestroyRenderState() override;
private:
	float ShadowResoulutionScale = 1.0f;
	float ShadowBias = 0.005f;
	float ShadowSlopeBias = 0.01f;
	float ShadowSharpen = 0.0f;

	float ShadowAngle = 60.0f;
	float ShadowNearPlane = 0.1f;
	float ShadowFarPlane = 100.0f;
};