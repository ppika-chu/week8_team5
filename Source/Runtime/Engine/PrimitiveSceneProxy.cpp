#include "EnginePCH.h"
#include "PrimitiveSceneProxy.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Render/Mesh.h"
#include "Engine/Scene.h"

static bool AreWorldMatricesEqual(const FMatrix& Previous, const FMatrix& Current)
{
    for (int32 Row = 0; Row < 4; ++Row)
    {
        for (int32 Column = 0; Column < 4; ++Column)
        {
            if (Previous.M[Row][Column] != Current.M[Row][Column])
            {
                return false;
            }
        }
    }

    return true;
}

FPrimitiveSceneProxy::~FPrimitiveSceneProxy()
{
    if (Mesh) Mesh->UnregisterProxy(this);
}

void FPrimitiveSceneProxy::OnMeshRenderDataChanged()
{
    if (Scene)
    {
        Scene->MarkRenderStateDirty(this);
        Scene->MarkDirty(this);
    }
}

void FPrimitiveSceneProxy::OnMeshDestroyed()
{
    Mesh = nullptr;
    RenderState.reset();
}

void FPrimitiveSceneProxy::UpdateTransform()
{
    const FMatrix NewLocalToWorld = Component->GetWorldMatrix();
    const bool bFirstUpdate = TransformRevision == 0;
    const bool bTransformChanged = bFirstUpdate || !AreWorldMatricesEqual(LocalToWorld, NewLocalToWorld);

    if (bTransformChanged)
    {
        ++TransformRevision;
    }

    LocalToWorld = NewLocalToWorld;
    WorldToLocal = LocalToWorld.Inverse();
    const FBox LocalBounds = Mesh ? Mesh->GetRenderBounds() : Component->CalcLocalBounds();
    Bounds = MakeWorldBounds(LocalBounds.GetWorldAABB(LocalToWorld));
    LODSphere = Mesh ? Mesh->GetLocalLODSphere().Transform(LocalToWorld) : FLODSphere{};
}

void FPrimitiveSceneProxy::UpdateRenderState()
{
    UStaticMesh* PreviousMesh = Mesh;
    Mesh = nullptr;
    RenderState.reset();
    bVisible = Component->IsVisible();
    bCastShadows = Component->IsCastingShadows();
    bReceiveShadows = Component->IsReceivingShadows();
    ShadowNormalOffset = Component->GetShadowNormalOffset();
    UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component);
    if (StaticMeshComponent) Mesh = StaticMeshComponent->GetStaticMesh();
    if (Mesh != PreviousMesh)
    {
        if (PreviousMesh) PreviousMesh->UnregisterProxy(this);
        if (Mesh) Mesh->RegisterProxy(this);
        if (Scene) Scene->MarkDirty(this);
    }
    if (!Mesh) return;
    std::vector<UMaterial*> Materials;
    Materials.reserve(Mesh->MeshData.MaterialSlots.Num());
    for (uint32 Slot = 0; Slot < static_cast<uint32>(Mesh->MeshData.MaterialSlots.Num()); ++Slot)
        Materials.push_back(StaticMeshComponent->GetMaterial(Slot));
    RenderState = Mesh->GetRenderState(Materials);
}
