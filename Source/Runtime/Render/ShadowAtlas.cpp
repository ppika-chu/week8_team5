#include "EnginePCH.h"
#include "ShadowAtlas.h"
#include "RenderCommand.h"

bool FShadowAtlas::Init(uint32 InAtlasSize)
{
	if (InAtlasSize == 0)
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
	ResetAllocations();
	return true;
}

void FShadowAtlas::ResetAllocations()
{
	Shelves.Reset();
	UsedHeight = 0;
	LastResolution = DefaultShadowResolution;
}

// Should be called in order of decreasing Resolution
// Shelf packing algorithm (First-fit)
bool FShadowAtlas::Allocate(uint32 Resolution, FShadowAtlasAllocation* OutAllocation)
{
	assert(OutAllocation);
	assert(Resolution <= LastResolution);
	LastResolution = Resolution;

	assert(Resolution > 0 && Resolution <= AtlasSize);

	// Select the first shelf that can fit the tile
	for (FShelf& Shelf : Shelves)
	{
		if (Resolution > Shelf.Height ||
			Resolution > AtlasSize - Shelf.NextX)
		{
			continue;
		}

		*OutAllocation = {
			.X = Shelf.NextX,
			.Y = Shelf.Y,
			.Width = Resolution,
			.Height = Resolution
		};

		Shelf.NextX += Resolution;
		return true;
	}

	// Create a new shelf if no existing shelf can fit the tile
	if (Resolution > AtlasSize - UsedHeight)
	{
		// No more space in the atlas
		return false;
	}

	Shelves.Add(FShelf{
		.Y = UsedHeight,
		.Height = Resolution,
		.NextX = Resolution
	});

	*OutAllocation = {
		.X = 0,
		.Y = UsedHeight,
		.Width = Resolution,
		.Height = Resolution
	};

	UsedHeight += Resolution;
	return true;
}
