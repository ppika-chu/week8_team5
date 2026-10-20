#include "EnginePCH.h"
#include "ShadowAtlas.h"
#include "RenderCommand.h"

bool FShadowAtlas::TryPack(
	const TArray<uint32>& Resolutions,
	uint32 CandidateSize,
	uint32 MaxLayers,
	TArray<FShadowAtlasAllocation>* OutAllocations,
	uint32* OutRequiredLayers)
{
	assert(OutAllocations || OutRequiredLayers);

	OutAllocations->Reset();
	*OutRequiredLayers = 0;

	if (CandidateSize == 0 || MaxLayers == 0)
	{
		return false;
	}

	//TArray<FShelf> Shelves;
	TArray<FLayerPackingState> Layers;
	TArray<FShadowAtlasAllocation> Allocations;
	Allocations.Reset(Resolutions.Num());

	for (uint32 Resolution : Resolutions)
	{
		if (Resolution == 0 || Resolution > CandidateSize)
		{
			return false;
		}

		FShadowAtlasAllocation ALlocation{};
		bool bPlaced = false;

		// Use the remaining space in existing layers first
		for (uint32 LayerIndex = 0;
			LayerIndex < Layers.Num();
			++LayerIndex)
		{
			if (TryPlaceInLayer(
				CandidateSize,
				Layers[LayerIndex],
				Resolution,
				LayerIndex,
				&ALlocation))
			{
				bPlaced = true;
				break;
			}
		}

		// If it doesn't fit in any existing layer, create a new layer
		if (!bPlaced)
		{
			if (Layers.Num() >= MaxLayers)
			{
				return false;
			}

			const uint32 NewLayerIndex = Layers.Num();
			Layers.Add(FLayerPackingState{});

			if (!TryPlaceInLayer(
				CandidateSize,
				Layers[NewLayerIndex],
				Resolution,
				NewLayerIndex,
				&ALlocation))
			{
				return false;
			}
		}

		Allocations.Add(ALlocation);
	}

	*OutRequiredLayers = Layers.Num();
	*OutAllocations = std::move(Allocations);
	return true;
}

bool FShadowAtlas::TryPlaceInLayer(
	uint32 CandidateSize,
	FLayerPackingState& Layer,
	uint32 Resolution,
	uint32 ArrayIndex,
	FShadowAtlasAllocation* OutAllocation)
{
	assert(OutAllocation);

	// First-fit shelf packing
	// Select the first shelf that can fit the tile
	for (FShelf& Shelf : Layer.Shelves)
	{
		if (Resolution > Shelf.Height ||
			Resolution > CandidateSize - Shelf.NextX)
		{
			continue;
		}

		*OutAllocation = FShadowAtlasAllocation{
			.X = Shelf.NextX,
			.Y = Shelf.Y,
			.Width = Resolution,
			.Height = Resolution,
			.ArrayIndex = ArrayIndex,
		};

		Shelf.NextX += Resolution;
		return true;
	}

	// Create a new shelf if no existing shelf can fit the tile
	if (Resolution > CandidateSize - Layer.UsedHeight)
	{
		// No more space in this layer
		return false;
	}

	const uint32 Y = Layer.UsedHeight;

	Layer.Shelves.Add(FShelf{
		.Y = Y,
		.Height = Resolution,
		.NextX = Resolution
		});
	Layer.UsedHeight += Resolution;

	*OutAllocation = FShadowAtlasAllocation{
		.X = 0,
		.Y = Y,
		.Width = Resolution,
		.Height = Resolution,
		.ArrayIndex = ArrayIndex,
	};

	return true;
}

bool FShadowAtlas::EnsureTextureSize(uint32 RequiredSize, uint32 RequiredLayers)
{
	if (RequiredSize == 0 || RequiredLayers == 0)
	{
		return false;
	}

	if (Texture && AtlasSize >= RequiredSize &&
		LayerCount >= RequiredLayers)
	{
		return true;
	}

	// Do not decrease the allocated space
	const uint32 NewSize = FMath::Max(AtlasSize, RequiredSize);
	const uint32 NewLayerCount = FMath::Max(
		LayerCount, RequiredLayers);

	D3D11_TEXTURE2D_DESC Desc{
		.Width = NewSize,
		.Height = NewSize,
		.MipLevels = 1,
		.ArraySize = NewLayerCount,
		.Format = DXGI_FORMAT_R32_TYPELESS,
		.SampleDesc = { 1, 0 },
		.Usage = D3D11_USAGE_DEFAULT,
		.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE,
	};

	// Always create a new texture as a texture 2d array
	auto NewTexture = RenderCommand::CreateTexture2D(Desc, nullptr, true);
	assert(NewTexture->GetDimension() == ETextureDimension::Texture2DArray);

	if (!NewTexture || !NewTexture->GetRawPtr() || !NewTexture->GetSRV())
	{
		return false;
	}

	for (uint32 LayerIndex = 0;
		LayerIndex < NewLayerCount;
		++LayerIndex)
	{
		if (!NewTexture->GetSliceDSV(LayerIndex))
		{
			return false;
		}
	}

	assert(
		NewTexture->GetDimension() ==
		ETextureDimension::Texture2DArray);

	if (Texture)
	{
		RenderCommand::UnbindShaderResource(
			13,
			EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
	}

	Texture = std::move(NewTexture);
	AtlasSize = NewSize;
	LayerCount = NewLayerCount;

	HTR_LOG(Info, "[ShadowAtlas] Created new texture: Size={}, Layers={}", NewSize, NewLayerCount);

	return true;
}