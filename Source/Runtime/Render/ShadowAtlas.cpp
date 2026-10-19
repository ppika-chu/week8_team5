#include "EnginePCH.h"
#include "ShadowAtlas.h"
#include "RenderCommand.h"

bool FShadowAtlas::Init(uint32 InAtlasSize, uint32 InTilesize)
{
	if (InAtlasSize == 0 || InTilesize == 0 ||
		InAtlasSize % InTilesize != 0)
	{
		return false;
	}

	D3D11_TEXTURE2D_DESC Desc{
		.Width = InAtlasSize,
		.Height = InAtlasSize,
		.MipLevels = 1,
		.ArraySize = 1,
		.Format = DXGI_FORMAT_R24G8_TYPELESS,
		.SampleDesc = { 1, 0 },
		.Usage = D3D11_USAGE_DEFAULT,
		.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE,
	};

	Texture = RenderCommand::CreateTexture2D(Desc);

	if (!Texture || !Texture->GetDSV() || !Texture->GetSRV())
	{
		return false;
	}

	AtlasSize = InAtlasSize;
	TileSize = InTilesize;
	TilesPerRow = AtlasSize / TileSize;
	NextTile = 0;
	return true;
}

void FShadowAtlas::ResetAllocations()
{
	NextTile = 0;
	LastResolutionScale = 1.0f;
}

// Should be called in order of decreasing ResolutionScale
bool FShadowAtlas::Allocate(float ResolutionScale, FShadowAtlasAllocation* OutAllocation)
{
	assert(OutAllocation);
	assert(ResolutionScale <= LastResolutionScale);
	LastResolutionScale = ResolutionScale;

	if (TilesPerRow == 0 || NextTile >= TilesPerRow * TilesPerRow)
	{
		return false;
	}

	OutAllocation->X = (NextTile % TilesPerRow) * TileSize;
	OutAllocation->Y = (NextTile / TilesPerRow) * TileSize;
	OutAllocation->Width = TileSize * ResolutionScale;
	OutAllocation->Height = TileSize * ResolutionScale;

	NextTile++;
	return true;
}
