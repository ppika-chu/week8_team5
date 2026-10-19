#pragma once

#include "Texture2D.h"

constexpr uint32 DefaultShadowResolution = 4096;

struct FShadowAtlasAllocation
{
	uint32 X = 0;
	uint32 Y = 0;
	uint32 Width = 0;
	uint32 Height = 0;
};

class FShadowAtlas
{
private:
	struct FShelf
	{
		uint32 Y = 0;		// The Y coordinate of the shelf in the atlas
		uint32 Height = 0;	// The height of the shelf
		uint32 NextX = 0;	// The next X coordinate to place a tile in the shelf
	};

public:
	//bool Init(uint32 InAtlasSize);

	//void ResetAllocations();

	//bool Allocate(uint32 Resolution, FShadowAtlasAllocation* OutAllocation);

	static bool TryPack(
		const TArray<uint32>& Resolutions,
		uint32 CandidateSize,
		TArray<FShadowAtlasAllocation>* OutAllocations
	);

	bool EnsureTextureSize(uint32 RequiredSize);

	FTexture2D* GetTexture() const { return Texture.get(); }
	uint32 GetSize() const { return AtlasSize; }

private:
	TUniquePtr<FTexture2D> Texture{};

	uint32 AtlasSize = 0;
	//uint32 UsedHeight = 0;	// Total height used by shelves
	//TArray<FShelf> Shelves{};

	uint32 LastResolution = DefaultShadowResolution;
};
