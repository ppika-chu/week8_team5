#include "EnginePCH.h"
#include "SpotLightSceneProxy.h"

#include "Components/SpotLightComponent.h"

void FSpotLightSceneProxy::GetLightShaderParameters(FLightRenderParameters& Out) const
{
    FPointLightSceneProxy::GetLightShaderParameters(Out);

    const auto* Spot = static_cast<const USpotLightComponent*>(LightComponent);
    FTransform Transform;
    Transform.Rotation = Spot->GetWorldRotation();
    const float Outer = FMath::Clamp(Spot->GetOuterConeAngle(), 0.0f, 89.0f);
    const float Inner = FMath::Clamp(Spot->GetInnerConeAngle(), 0.0f, Outer);

    Out.Type = static_cast<uint32>(ELightType::Spot);
    Out.Direction = Transform.GetForward();
    Out.InnerConeCos = std::cos(FMath::DegreesToRadians(Inner));
    Out.OuterConeCos = std::cos(FMath::DegreesToRadians(Outer));
}

FMatrix FSpotLightSceneProxy::GetShadowViewMatrix() const
{
    const FVector WorldLightLocation = LightComponent->GetWorldLocation();
    const FMatrix RotationMatrix = LightComponent->GetWorldRotation().Quaternion().ToFMatrix();

    const FMatrix InverseTranslationMatrix = FMatrix::MakeTranslation(WorldLightLocation * -1.0f);
    const FMatrix InverseRotationMatrix = RotationMatrix.GetTransposed();

    return InverseTranslationMatrix * InverseRotationMatrix;
}

FMatrix FSpotLightSceneProxy::GetShadowProjectionMatrix() const
{
    // Todo: Need to prove
    const auto* SpotLight = static_cast<const USpotLightComponent*>(LightComponent);
    const float HalfAngleRadians = FMath::DegreesToRadians(FMath::Clamp(SpotLight->GetOuterConeAngle(), 0.5f, 89.0f));

    const float ProjectionScale = 1.0f / std::tan(HalfAngleRadians);
    const float NearClip = 0.1f;
    const float FarClip = std::max(SpotLight->GetAttenuationRadius(), NearClip + 0.1f);

    const float DepthScale = FarClip / (FarClip - NearClip);
    const float DepthOffset = -NearClip * DepthScale;

    // 카메라와 동일한 x 전방
    return FMatrix(
        0.0f, 0.0f, DepthScale, 1.0f,
        ProjectionScale, 0.0f, 0.0f, 0.0f,
        0.0f, ProjectionScale, 0.0f, 0.0f,
        0.0f, 0.0f, DepthOffset, 0.0f
    );
}
