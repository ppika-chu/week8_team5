#pragma once

#include "Texture2D.h"

struct FShadowAtlasAllocation
{
	uint32 X = 0;
	uint32 Y = 0;
	uint32 Width = 0;
	uint32 Height = 0;
};

class FShadowAtlas
{
public:
	bool Init(uint32 InAtlasSize, uint32 InTileSize);

	void ResetAllocations() { NextTile = 0; }

	bool Allocate(FShadowAtlasAllocation* OutAllocation);

	FTexture2D* GetTexture() const { return Texture.get(); }
	uint32 GetSize() const { return AtlasSize; }
	uint32 GetTilesize() const { return TileSize; }
	uint32 GetRemainingTileCount() const
	{
		return TilesPerRow * TilesPerRow - NextTile;
	}

private:
	TUniquePtr<FTexture2D> Texture{};

	uint32 AtlasSize = 0;
	uint32 TileSize = 0;
	uint32 TilesPerRow = 0;
	uint32 NextTile = 0;
};
