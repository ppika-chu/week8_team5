#include "EnginePCH.h"

#include "ShadowMapManager.h"
#include "RenderCommand.h"

constexpr uint32 DefaultShadowMapResolution = 1024;

FTexture2D* FShadowMapManager::GetOrCreateShadowMap(uint32 LightID)
{
	if (auto* ShadowMap = ShadowMaps.Find(LightID))
	{
		return ShadowMap->get();
	}

	// Create a new shadow map resource
	D3D11_TEXTURE2D_DESC TextureDesc{
		.Width = DefaultShadowMapResolution,
		.Height = DefaultShadowMapResolution,
		.MipLevels = 1,
		.ArraySize = 1,
		.Format = DXGI_FORMAT_R32_TYPELESS,
		.SampleDesc = {
			.Count = 1,
			.Quality = 0
		},
		.Usage = D3D11_USAGE_DEFAULT,
		.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE,
		.CPUAccessFlags = 0,
		.MiscFlags = 0,
	};

	TUniquePtr<FTexture2D> Texture =
		RenderCommand::CreateTexture2D(TextureDesc);

	if (!Texture || !Texture->GetRawPtr() ||
		!Texture->GetDSV() || !Texture->GetSRV())
	{
		return nullptr;
	}

	FTexture2D* NewShadowMap = Texture.get();
	ShadowMaps[LightID] = std::move(Texture);
	return NewShadowMap;
}

void FShadowMapManager::ReleaseShadowMap(uint32 LightID)
{
	ShadowMaps.Remove(LightID);
}