#pragma once

#include <imgui.h>
#include <Windows.h>
#include <wrl/client.h>

#include <d3d11.h>

#include "Core/Types.h"

class FImGuiRenderer
{
private:
	struct alignas(16) FArrayPreviewConstants
	{
		uint32 ArrayIndex = 0;
		uint32 bDepthPreview = 1;
		float Padding[2] = { 0.0f, 0.0f };
	};

	struct FArrayPreviewCallbackData
	{
		FImGuiRenderer* Renderer = nullptr;
		uint32 ArrayIndex = 0;
		uint32 bDepthPreview = 1;
	};

public:
	FImGuiRenderer() = default;
	~FImGuiRenderer();

	bool Init(HWND WindowHandle, ID3D11Device* Device, ID3D11DeviceContext* DeviceContext);
	void Begin();
	void End();
	void Shutdown();

	void ImageTextureArray(
		ID3D11ShaderResourceView* ArraySRV,
		uint32 ArrayIndex,
		const ImVec2& Size,
		const ImVec2& UV0 = ImVec2(0, 0),
		const ImVec2& UV1 = ImVec2(1, 1),
		bool bDepthPreview = true);

private:
	bool CreateArrayPreviewResources(ID3D11Device* Device);

	static void SetupArrayPreviewCallback(
		const ImDrawList* DrawList,
		const ImDrawCmd* DrawCmd);

	static void RestoreArrayPreviewCallback(
		const ImDrawList* DrawList,
		const ImDrawCmd* DrawCmd);
	

private:
	ComPtr<ID3D11PixelShader> ArrayPreviewPS;
	ComPtr<ID3D11Buffer> ArrayPreviewCB;

	// Saved state for restoring after array preview rendering
	ComPtr<ID3D11Buffer> SavedPSConstantBuffer;
	bool bArrayPreviewActive = false;

	// Keep the life of SRV before the real rendering
	TArray<ComPtr<ID3D11ShaderResourceView>> FramePreviewSRVs;
};