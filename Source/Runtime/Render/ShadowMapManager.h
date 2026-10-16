#pragma once
#include <wrl/client.h>
#include "Core/Types.h"
#include "Texture2D.h"
#include "RenderingInfo.h"

//struct FShadowMapResource
//{
//	ComPtr<ID3D11Texture2D> Texture;
//	ComPtr<ID3D11DepthStencilView> DSV;
//	ComPtr<ID3D11ShaderResourceView> SRV;
//
//	uint32 Resolution = 1024;
//};

class FShadowMapManager
{
public:
	FTexture2D* GetOrCreateShadowMap(uint32 LightID);
	FTexture2D* GetOrCreateShadowMap(uint32 LightID, uint32 Resolution);
	FTexture2D* GetShadowMapOrNull(uint32 LightID) const;
	FShadowInfo GetShadowInfo(uint32 LightID, uint32 Resolution);
	void ReleaseShadowMap(uint32 LightID);


private:
	TMap<uint32, TUniquePtr<FTexture2D>> ShadowMaps;
};