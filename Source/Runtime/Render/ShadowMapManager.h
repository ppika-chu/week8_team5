#pragma once
#include <wrl/client.h>
#include "Core/Types.h"
#include "Texture2D.h"

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
	void ReleaseShadowMap(uint32 LightID);


private:
	TMap<uint32, TUniquePtr<FTexture2D>> ShadowMaps;
};