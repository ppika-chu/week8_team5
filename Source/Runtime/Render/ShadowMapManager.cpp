#include "EnginePCH.h"

#include "ShadowMapManager.h"
#include "RenderCommand.h"

constexpr uint32 DefaultShadowMapResolution = 1024;

FTexture2D* FShadowMapManager::GetOrCreateShadowMap(uint32 LightID)
{
	return GetOrCreateShadowMap(LightID, DefaultShadowMapResolution);
}

FTexture2D* FShadowMapManager::GetOrCreateShadowMap(uint32 LightID, uint32 Resolution)
{
	if (auto* ShadowMap = ShadowMaps.Find(LightID))
	{
		if (ShadowMap->get()->GetWidth() != Resolution ||
			ShadowMap->get()->GetHeight() != Resolution)
		{
			ShadowMaps.Remove(LightID);
		}
		else
		{
			return ShadowMap->get();
		}
	}

	// Create a new shadow map resource
	D3D11_TEXTURE2D_DESC TextureDesc{
		.Width = Resolution,
		.Height = Resolution,
		.MipLevels = 1,
		.ArraySize = 1,
		.Format = DXGI_FORMAT_R24G8_TYPELESS,
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
		assert(false);
		return nullptr;
	}

	FTexture2D* NewShadowMap = Texture.get();
	ShadowMaps[LightID] = std::move(Texture);
	return NewShadowMap;
}

FTexture2D* FShadowMapManager::GetShadowMapOrNull(uint32 LightID) const
{
	if (const TUniquePtr<FTexture2D>* ShadowMap = ShadowMaps.Find(LightID))
	{
		return ShadowMap->get();
	}
	return nullptr;
}

FShadowInfo FShadowMapManager::GetShadowInfo(uint32 LightID, uint32 Resolution)
{	
	FShadowInfo Info;
	if (FTexture2D* ShadowMap = GetOrCreateShadowMap(LightID, Resolution))
	{
		Info.ShadowMap.Texture = ShadowMap;
		Info.ShadowMap.LoadOp = ERenderTargetLoadOp::Clear;
		Info.ShadowMap.StoreOp = ERenderTargetStoreOp::Store;
		Info.ShadowMap.ClearValue.DepthClearValue = 1.0f;
		Info.ShadowMap.ClearValue.StencilClearValue = 0;
		Info.resolution = Resolution;
	}
	else
	{
		Info.ShadowMap.Texture = nullptr;
		Info.resolution = 0;
	}
	return Info;
}

void FShadowMapManager::ReleaseShadowMap(uint32 LightID)
{
	ShadowMaps.Remove(LightID);
}