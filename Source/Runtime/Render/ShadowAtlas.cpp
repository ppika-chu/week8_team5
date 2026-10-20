#include "EnginePCH.h"
#include "ShadowAtlas.h"
#include "RenderCommand.h"

bool FShadowAtlas::TryPack(
	const TArray<uint32>& Resolutions,
	uint32 CandidateSize,
	TArray<FShadowAtlasAllocation>* OutAllocations)
{
	assert(OutAllocations);

	OutAllocations->Reset();

	if (CandidateSize == 0)
	{
		return false;
	}

	TArray<FShelf> Shelves;
	TArray<FShadowAtlasAllocation> Allocations;
	Allocations.Reset(Resolutions.Num());

	uint32 UsedHeight = 0;

	for (uint32 Resolution : Resolutions)
	{
		if (Resolution == 0 || Resolution > CandidateSize)
		{
			return false;
		}

		bool bPlaced = false;

		// First-fit shelf packing
		// Select the first shelf that can fit the tile
		for (FShelf& Shelf : Shelves)
		{
			if (Resolution > Shelf.Height ||
				Resolution > CandidateSize - Shelf.NextX)
			{
				continue;
			}

			Allocations.Add(FShadowAtlasAllocation{
				.X = Shelf.NextX,
				.Y = Shelf.Y,
				.Width = Resolution,
				.Height = Resolution
				});

			Shelf.NextX += Resolution;
			bPlaced = true;
			break;
		}

		if (bPlaced)
		{
			continue;
		}

		// Create a new shelf if no existing shelf can fit the tile
		if (Resolution > CandidateSize - UsedHeight)
		{
			// No more space in the atlas
			return false;
		}

		Shelves.Add(FShelf{
			.Y = UsedHeight,
			.Height = Resolution,
			.NextX = Resolution
			});

		Allocations.Add(FShadowAtlasAllocation{
			.X = 0,
			.Y = UsedHeight,
			.Width = Resolution,
			.Height = Resolution
			});

		UsedHeight += Resolution;
	}

	*OutAllocations = std::move(Allocations);
	return true;
}

bool FShadowAtlas::EnsureTextureSize(uint32 RequiredSize)
{
	if (RequiredSize == 0)
	{
		return false;
	}

	if (Texture && AtlasSize >= RequiredSize)
	{
		return true;
	}

	D3D11_TEXTURE2D_DESC Desc{
		.Width = RequiredSize,
		.Height = RequiredSize,
		.MipLevels = 1,
		.ArraySize = 1,
		.Format = DXGI_FORMAT_R32_TYPELESS,
		.SampleDesc = { 1, 0 },
		.Usage = D3D11_USAGE_DEFAULT,
		.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE,
	};

	auto NewTexture = RenderCommand::CreateTexture2D(Desc);

	if (!NewTexture || !NewTexture->GetDSV() || !NewTexture->GetSRV())
	{
		return false;
	}

	if (Texture)
	{
		RenderCommand::UnbindShaderResource(
			12,
			EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
	}

	Texture = std::move(NewTexture);
	AtlasSize = RequiredSize;
	return true;
}