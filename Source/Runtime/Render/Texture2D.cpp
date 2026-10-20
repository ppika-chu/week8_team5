#include "EnginePCH.h"
#include "Texture2D.h"

#include "RenderCommand.h"


FTexture2D::FTexture2D(ID3D11Device* Device, const D3D11_TEXTURE2D_DESC& InDesc, const FImageData& Image)
	: FTexture2D(Device, InDesc, Image.IsValid() ? Image.Pixels.GetData() : nullptr, Image.GetRowPitch())
{
}

FTexture2D::FTexture2D(ID3D11Device* Device, const D3D11_TEXTURE2D_DESC& InDesc, const void* InitialData, uint32 RowPitch, bool bFroceArrayView)
{
	Width = InDesc.Width;
	Height = InDesc.Height;
	Depth = 1;
	Format = InDesc.Format;

	if (bFroceArrayView)
	{
		Dimension = ETextureDimension::Texture2DArray;
	}
	else if (InDesc.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE)
	{
		Dimension = ETextureDimension::TextureCube;
	}
	else if (InDesc.ArraySize > 1)
	{
		Dimension = ETextureDimension::Texture2DArray;
	}
	else
	{
		Dimension = ETextureDimension::Texture2D;
	}

	if (InitialData && RowPitch == 0)
	{
		const uint32 BytesPerPixel = FormatToBytes(InDesc.Format);
		if (BytesPerPixel == 0) { /* 기존 에러 처리 그대로 */ return; }
		RowPitch = InDesc.Width * BytesPerPixel;
	}

	const bool bGenerateMips = (InDesc.MiscFlags & D3D11_RESOURCE_MISC_GENERATE_MIPS) != 0;

	D3D11_SUBRESOURCE_DATA SubData{ InitialData, RowPitch, 0 };
	const D3D11_SUBRESOURCE_DATA* InitPtr = (InitialData && !bGenerateMips) ? &SubData : nullptr;

	HRESULT hr = Device->CreateTexture2D(&InDesc, InitPtr, (ID3D11Texture2D**)Texture.GetAddressOf());
	if (FAILED(hr)) { /* 기존 로그 그대로 */ return; }

	CreateViews(Device, InDesc);

	if (bGenerateMips && InitialData && SRV)
	{
		ComPtr<ID3D11DeviceContext> Context;
		Device->GetImmediateContext(Context.GetAddressOf());
		Context->UpdateSubresource(Texture.Get(), 0, nullptr, InitialData, RowPitch, 0); // 0번 레벨
		Context->GenerateMips(SRV.Get());                                              // 나머지 레벨
	}
}
FTexture2D::FTexture2D(ID3D11Device* Device, ComPtr<ID3D11Resource> SwapchainTexture, const D3D11_TEXTURE2D_DESC& InDesc)
{
	Texture = std::move(SwapchainTexture);

	Width = InDesc.Width;
	Height = InDesc.Height;
	Depth = 1;
	Format = InDesc.Format;
	Dimension = ETextureDimension::Texture2D;

	CreateViews(Device, InDesc);
}

ID3D11DepthStencilView* FTexture2D::GetSliceDSV(uint32 ArrayIndex) const
{
	if (Dimension != ETextureDimension::Texture2DArray)
	{
		return ArrayIndex == 0 ? DSV.Get() : nullptr;
	}

	return SliceDSVs.IsValidIndex(ArrayIndex)
		? SliceDSVs[ArrayIndex].Get()
		: nullptr;
}

void FTexture2D::CreateViews(ID3D11Device* Device, const D3D11_TEXTURE2D_DESC& InDesc)
{
	HRESULT hr;

	// TYPELESS 깊이 텍스처는 뷰마다 해석할 포맷을 지정해야 한다 (nullptr이면 생성 실패)
	const bool bTypelessDepth = (InDesc.Format == DXGI_FORMAT_R24G8_TYPELESS || InDesc.Format == DXGI_FORMAT_R32_TYPELESS);
	const bool bArrayView = Dimension == ETextureDimension::Texture2DArray;
	D3D11_SHADER_RESOURCE_VIEW_DESC SrvDesc{};
	D3D11_DEPTH_STENCIL_VIEW_DESC DsvDesc{};
	switch (InDesc.Format)
	{
	case DXGI_FORMAT_R32_TYPELESS:
		SrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
		DsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
		break;
	case DXGI_FORMAT_R24G8_TYPELESS:
		SrvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
		DsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		break;
	default:
		SrvDesc.Format = InDesc.Format;
		DsvDesc.Format = InDesc.Format;
		break;
	}

	if (InDesc.BindFlags & D3D11_BIND_SHADER_RESOURCE)
	{
		if (bArrayView)
		{
			SrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
			SrvDesc.Texture2DArray = {
				.MostDetailedMip = 0,
				.MipLevels = InDesc.MipLevels == 0
					? UINT(-1) : InDesc.MipLevels,
				.FirstArraySlice = 0,
				.ArraySize = InDesc.ArraySize
			};
		}
		else
		{
			SrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			SrvDesc.Texture2D.MipLevels = 1;
		}

		const D3D11_SHADER_RESOURCE_VIEW_DESC* ViewDesc =
			(bTypelessDepth || bArrayView) ? &SrvDesc : nullptr;

		hr = Device->CreateShaderResourceView(Texture.Get(), ViewDesc, SRV.GetAddressOf());
		if (FAILED(hr))
			HTR_LOG(Error, "[Texture2D] CreateShaderResourceView failed (hr=0x{:08X})", (uint32)hr);
	}

	if ((InDesc.BindFlags & D3D11_BIND_RENDER_TARGET) &&
		!(InDesc.MiscFlags & D3D11_RESOURCE_MISC_GENERATE_MIPS))
	{
		hr = Device->CreateRenderTargetView(Texture.Get(), nullptr, RTV.GetAddressOf());
		if (FAILED(hr))
			HTR_LOG(Error, "[Texture2D] CreateRenderTargetView failed (hr=0x{:08X})", (uint32)hr);
	}

	if (InDesc.BindFlags & D3D11_BIND_DEPTH_STENCIL)
	{
		if (bArrayView)
		{
			SliceDSVs.Reset();
			DSV.Reset();

			for (uint32 ArrayIndex = 0;
				ArrayIndex < InDesc.ArraySize;
				++ArrayIndex)
			{
				DsvDesc.ViewDimension =
					D3D11_DSV_DIMENSION_TEXTURE2DARRAY;

				DsvDesc.Texture2DArray = {
					.MipSlice = 0,
					.FirstArraySlice = ArrayIndex,
					.ArraySize = 1
				};

				ComPtr<ID3D11DepthStencilView> SliceDSV;

				hr = Device->CreateDepthStencilView(
					Texture.Get(),
					&DsvDesc,
					SliceDSV.GetAddressOf());

				if (FAILED(hr))
				{
					HTR_LOG(Error, "[Texture2D] CreateDepthStencilView for slice {} failed (hr=0x{:08X})", ArrayIndex, (uint32)hr);
					SliceDSVs.Reset();
					return;
				}

				SliceDSVs.Add(std::move(SliceDSV));
			}

			if (SliceDSVs.Num() > 0)
			{
				DSV = SliceDSVs[0];
			}
		}
		else
		{
			DsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;

			const D3D11_DEPTH_STENCIL_VIEW_DESC* ViewDesc =
				bTypelessDepth ? &DsvDesc : nullptr;

			hr = Device->CreateDepthStencilView(Texture.Get(), ViewDesc, DSV.GetAddressOf());
			if (FAILED(hr))
				HTR_LOG(Error, "[Texture2D] CreateDepthStencilView failed (hr=0x{:08X})", (uint32)hr);
		}

	}
}

UTexture2D::UTexture2D()
{

}
UTexture2D::~UTexture2D()
{
}

