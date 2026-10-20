#include "EnginePCH.h"
#include "Editor/EditorUI/ImGuiRenderer.h"

#include "Core/Window.h"
#include "Input/InputSystem.h"

#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace
{
	// FWindow가 ImGui를 직접 알지 않도록 메시지 처리기를 훅으로 등록한다.
	bool ImGuiWndProcHook(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		return ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam) != 0;
	}

	void ApplyDefaultStyle()
	{
		ImGuiStyle& style = ImGui::GetStyle();
		ImVec4* colors = style.Colors;

		// Accent
		const ImVec4 Accent = ImVec4(0.65f, 0.30f, 0.45f, 1.00f);
		const ImVec4 AccentHover = ImVec4(0.75f, 0.40f, 0.55f, 1.00f);
		const ImVec4 AccentActive = ImVec4(0.55f, 0.20f, 0.35f, 1.00f);

		// Text
		colors[ImGuiCol_Text] =
			ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
		colors[ImGuiCol_TextDisabled] =
			ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
		colors[ImGuiCol_TextLink] = AccentHover;
		colors[ImGuiCol_TextSelectedBg] =
			ImVec4(1.00f, 1.00f, 1.00f, 0.156f);

		// Window Background
		colors[ImGuiCol_WindowBg] =
			ImVec4(0.180f, 0.180f, 0.180f, 1.00f);
		colors[ImGuiCol_ChildBg] =
			ImVec4(0.280f, 0.280f, 0.280f, 0.00f);
		colors[ImGuiCol_PopupBg] =
			ImVec4(0.313f, 0.313f, 0.313f, 1.00f);
		colors[ImGuiCol_DockingEmptyBg] =
			colors[ImGuiCol_WindowBg];

		// Borders
		colors[ImGuiCol_Border] =
			ImVec4(0.266f, 0.266f, 0.266f, 1.00f);
		colors[ImGuiCol_BorderShadow] =
			ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

		// Input Frames
		colors[ImGuiCol_FrameBg] =
			ImVec4(0.160f, 0.160f, 0.160f, 1.00f);
		colors[ImGuiCol_FrameBgHovered] =
			ImVec4(0.200f, 0.200f, 0.200f, 1.00f);
		colors[ImGuiCol_FrameBgActive] =
			ImVec4(0.280f, 0.280f, 0.280f, 1.00f);
		colors[ImGuiCol_InputTextCursor] =
			ImVec4(1.00f, 1.00f, 1.00f, 1.00f);

		// Title & Menu
		colors[ImGuiCol_TitleBg] =
			ImVec4(0.148f, 0.148f, 0.148f, 1.00f);
		colors[ImGuiCol_TitleBgActive] =
			ImVec4(0.148f, 0.148f, 0.148f, 1.00f);
		colors[ImGuiCol_TitleBgCollapsed] =
			ImVec4(0.148f, 0.148f, 0.148f, 1.00f);
		colors[ImGuiCol_MenuBarBg] =
			ImVec4(0.195f, 0.195f, 0.195f, 1.00f);

		// Scrollbar
		colors[ImGuiCol_ScrollbarBg] =
			ImVec4(0.160f, 0.160f, 0.160f, 1.00f);
		colors[ImGuiCol_ScrollbarGrab] =
			ImVec4(0.277f, 0.277f, 0.277f, 1.00f);
		colors[ImGuiCol_ScrollbarGrabHovered] =
			ImVec4(0.360f, 0.360f, 0.360f, 1.00f);
		colors[ImGuiCol_ScrollbarGrabActive] = AccentActive;

		// Checkbox
		// 선택된 박스는 분홍, 체크 표시는 흰색
		colors[ImGuiCol_CheckboxSelectedBg] =
			ImVec4(Accent.x, Accent.y, Accent.z, 0.80f);
		colors[ImGuiCol_CheckMark] =
			ImVec4(1.00f, 1.00f, 1.00f, 1.00f);

		// Slider
		colors[ImGuiCol_SliderGrab] =
			ImVec4(0.391f, 0.391f, 0.391f, 1.00f);
		colors[ImGuiCol_SliderGrabActive] = AccentHover;

		// Button
		colors[ImGuiCol_Button] =
			ImVec4(Accent.x, Accent.y, Accent.z, 0.80f);
		colors[ImGuiCol_ButtonHovered] = AccentHover;
		colors[ImGuiCol_ButtonActive] = AccentActive;

		// Header, TreeNode, Selectable
		colors[ImGuiCol_Header] =
			ImVec4(0.313f, 0.313f, 0.313f, 1.00f);
		colors[ImGuiCol_HeaderHovered] =
			ImVec4(0.400f, 0.400f, 0.400f, 1.00f);
		colors[ImGuiCol_HeaderActive] =
			ImVec4(0.469f, 0.469f, 0.469f, 1.00f);

		// Separator
		colors[ImGuiCol_Separator] = colors[ImGuiCol_Border];
		colors[ImGuiCol_SeparatorHovered] =
			ImVec4(0.391f, 0.391f, 0.391f, 1.00f);
		colors[ImGuiCol_SeparatorActive] = Accent;

		// Resize Grip
		colors[ImGuiCol_ResizeGrip] =
			ImVec4(1.00f, 1.00f, 1.00f, 0.25f);
		colors[ImGuiCol_ResizeGripHovered] =
			ImVec4(1.00f, 1.00f, 1.00f, 0.67f);
		colors[ImGuiCol_ResizeGripActive] = Accent;

		// Tabs
		colors[ImGuiCol_Tab] =
			ImVec4(0.098f, 0.098f, 0.098f, 1.00f);
		colors[ImGuiCol_TabHovered] =
			ImVec4(0.352f, 0.352f, 0.352f, 1.00f);
		colors[ImGuiCol_TabSelected] =
			ImVec4(0.250f, 0.250f, 0.250f, 1.00f);
		colors[ImGuiCol_TabDimmed] =
			ImVec4(0.098f, 0.098f, 0.098f, 1.00f);
		colors[ImGuiCol_TabDimmedSelected] =
			ImVec4(0.195f, 0.195f, 0.195f, 1.00f);
		colors[ImGuiCol_TabSelectedOverline] = Accent;
		colors[ImGuiCol_TabDimmedSelectedOverline] =
			ImVec4(0.40f, 0.30f, 0.35f, 1.00f);

		// Docking
		colors[ImGuiCol_DockingPreview] =
			ImVec4(Accent.x, Accent.y, Accent.z, 0.65f);

		// Graphs
		colors[ImGuiCol_PlotLines] =
			ImVec4(0.469f, 0.469f, 0.469f, 1.00f);
		colors[ImGuiCol_PlotLinesHovered] = AccentHover;
		colors[ImGuiCol_PlotHistogram] =
			ImVec4(0.586f, 0.586f, 0.586f, 1.00f);
		colors[ImGuiCol_PlotHistogramHovered] = AccentHover;

		// Tables
		colors[ImGuiCol_TableHeaderBg] =
			ImVec4(0.240f, 0.240f, 0.240f, 1.00f);
		colors[ImGuiCol_TableBorderStrong] =
			colors[ImGuiCol_Border];
		colors[ImGuiCol_TableBorderLight] =
			ImVec4(0.220f, 0.220f, 0.220f, 1.00f);
		colors[ImGuiCol_TableRowBg] =
			ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		colors[ImGuiCol_TableRowBgAlt] =
			ImVec4(1.00f, 1.00f, 1.00f, 0.03f);

		// Misc
		colors[ImGuiCol_TreeLines] =
			colors[ImGuiCol_Border];
		colors[ImGuiCol_DragDropTarget] = AccentHover;
		colors[ImGuiCol_DragDropTargetBg] =
			ImVec4(Accent.x, Accent.y, Accent.z, 0.18f);
		colors[ImGuiCol_UnsavedMarker] = Accent;
		colors[ImGuiCol_NavCursor] = Accent;
		colors[ImGuiCol_NavWindowingHighlight] = Accent;
		colors[ImGuiCol_NavWindowingDimBg] =
			ImVec4(0.00f, 0.00f, 0.00f, 0.586f);
		colors[ImGuiCol_ModalWindowDimBg] =
			ImVec4(0.00f, 0.00f, 0.00f, 0.586f);

		// ------------------------------------------------
		// Layout & Spacing
		// ------------------------------------------------

		// Padding
		style.WindowPadding = ImVec2(10.0f, 10.0f);
		style.FramePadding = ImVec2(8.0f, 4.0f);
		style.ItemSpacing = ImVec2(8.0f, 6.0f);
		style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
		style.CellPadding = ImVec2(6.0f, 4.0f);
		style.IndentSpacing = 18.0f;

		// Widget Sizes
		style.GrabMinSize = 10.0f;
		style.ScrollbarSize = 14.0f;

		// Rounding
		style.WindowRounding = 4.0f;
		style.ChildRounding = 4.0f;
		style.FrameRounding = 3.0f;
		style.PopupRounding = 4.0f;
		style.ScrollbarRounding = 7.0f;
		style.TabRounding = 3.0f;

		// Borders
		style.FrameBorderSize = 1.0f;
		style.TabBorderSize = 1.0f;

		// Minimum Window Size
		style.WindowMinSize = ImVec2(160.0f, 100.0f);
	}

}

FImGuiRenderer::~FImGuiRenderer()
{

}

bool FImGuiRenderer::Init(HWND WindowHandle, ID3D11Device* Device, ID3D11DeviceContext* DeviceContext)
{
	if (!Device || !DeviceContext)
	{
		return false;
	}

	if (!CreateArrayPreviewResources(Device))
	{
		return false;
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

	ImGui_ImplWin32_Init(WindowHandle);
	ImGui_ImplDX11_Init(Device, DeviceContext);
	FWindow::SetWndProcHook(&ImGuiWndProcHook);

	ApplyDefaultStyle();

	//// 프로세스가 Per-Monitor DPI Aware라 창이 실제 픽셀 크기로 잡힌다. 모니터 배율(예: 150%)만큼 UI를 키운다.
	//// 1.92+ 동적 폰트 아틀라스라 FontScaleDpi로 키워도 글자가 해당 크기로 다시 래스터화되어 흐려지지 않는다.
	//const float DpiScale = ImGui_ImplWin32_GetDpiScaleForHwnd(WindowHandle);
	//ImGuiStyle& Style = ImGui::GetStyle();
	//Style.ScaleAllSizes(DpiScale);     // 패딩·간격·스크롤바 등 크기 (ApplyDefaultStyle 이후에 적용)
	//Style.FontScaleDpi = DpiScale;     // 글자 크기
	//// 다른 배율의 모니터로 창을 옮기면 글자 배율을 자동으로 갱신한다.
	//io.ConfigDpiScaleFonts = true;
	//io.ConfigDpiScaleViewports = true;

	return true;
}

void FImGuiRenderer::Begin()
{
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();

	ImGui::NewFrame();

	ImGuiIO& io = ImGui::GetIO(); (void)io;
}

void FImGuiRenderer::End()
{
	ImGuiIO& io = ImGui::GetIO();

	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

	ImGui::UpdatePlatformWindows();
	ImGui::RenderPlatformWindowsDefault();

	assert(!bArrayPreviewActive);

	FramePreviewSRVs.Reset();
}

void FImGuiRenderer::Shutdown()
{
	FramePreviewSRVs.Reset();
	SavedPSConstantBuffer.Reset();
	bArrayPreviewActive = false;

	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();

	ArrayPreviewCB.Reset();
	ArrayPreviewPS.Reset();
}

bool FImGuiRenderer::CreateArrayPreviewResources(ID3D11Device* Device)
{
	if (!Device)
	{
		return false;
	}

	static constexpr char ShaderSource[] = R"(
Texture2DArray<float4> PreviewTexture : register(t0);
SamplerState PreviewSampler : register(s0);

cbuffer PreviewConstants : register(b0)
{
    uint ArrayIndex;
    uint bDepthPreview;
    float2 Padding;
};

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR0;
    float2 UV : TEXCOORD0;
};

float4 mainPS(PS_INPUT Input) : SV_Target
{
    float4 Value = PreviewTexture.SampleLevel(
        PreviewSampler,
        float3(Input.UV, (float)ArrayIndex),
        0);

    if (bDepthPreview != 0)
    {
        Value = float4(Value.rrr, 1.0f);
    }

    return Input.Color * Value;
}
)";

	ComPtr<ID3DBlob> ByteCode;
	ComPtr<ID3DBlob> Errors;

	UINT CompileFlags = D3DCOMPILE_ENABLE_STRICTNESS;

#if defined(_DEBUG)
	CompileFlags |=
		D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
	CompileFlags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

	HRESULT Result = D3DCompile(
		ShaderSource,
		sizeof(ShaderSource) - 1,
		"ImGuiTextureArrayPreview",
		nullptr,
		nullptr,
		"mainPS",
		"ps_5_0",
		CompileFlags,
		0,
		ByteCode.GetAddressOf(),
		Errors.GetAddressOf());

	if (FAILED(Result))
	{
		if (Errors)
		{
			HTR_LOG(
				Error,
				"[ImGui] Array preview shader compilation failed: {}",
				static_cast<const char*>(Errors->GetBufferPointer()));
		}

		return false;
	}

	ComPtr<ID3D11PixelShader> NewPS;

	Result = Device->CreatePixelShader(
		ByteCode->GetBufferPointer(),
		ByteCode->GetBufferSize(),
		nullptr,
		NewPS.GetAddressOf());

	if (FAILED(Result))
	{
		HTR_LOG(
			Error,
			"[ImGui] Array preview PS creation failed: 0x{:08X}",
			static_cast<uint32>(Result));

		return false;
	}

	D3D11_BUFFER_DESC BufferDesc{};
	BufferDesc.ByteWidth = sizeof(FArrayPreviewConstants);
	BufferDesc.Usage = D3D11_USAGE_DEFAULT;
	BufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

	Microsoft::WRL::ComPtr<ID3D11Buffer> NewCB;

	Result = Device->CreateBuffer(
		&BufferDesc,
		nullptr,
		NewCB.GetAddressOf());

	if (FAILED(Result))
	{
		HTR_LOG(
			Error,
			"[ImGui] Array preview CB creation failed: 0x{:08X}",
			static_cast<uint32>(Result));

		return false;
	}

	ArrayPreviewPS = std::move(NewPS);
	ArrayPreviewCB = std::move(NewCB);

	return true;
}

void FImGuiRenderer::SetupArrayPreviewCallback(
	const ImDrawList*,
	const ImDrawCmd* DrawCmd)
{
	const auto* Data =
		static_cast<const FArrayPreviewCallbackData*>(
			DrawCmd->UserCallbackData);

	auto* RenderState =
		static_cast<ImGui_ImplDX11_RenderState*>(
			ImGui::GetPlatformIO().Renderer_RenderState);

	assert(Data && Data->Renderer);
	assert(RenderState && RenderState->DeviceContext);

	if (!Data || !Data->Renderer ||
		!RenderState || !RenderState->DeviceContext)
	{
		return;
	}

	FImGuiRenderer& Renderer = *Data->Renderer;
	ID3D11DeviceContext* Context = RenderState->DeviceContext;

	assert(!Renderer.bArrayPreviewActive);

	// ImGui의 기본 상태 복원은 PS 상수 버퍼를 복원하지 않는다.
	Context->PSGetConstantBuffers(
		0,
		1,
		Renderer.SavedPSConstantBuffer.ReleaseAndGetAddressOf());

	const FArrayPreviewConstants Constants{
		.ArrayIndex = Data->ArrayIndex,
		.bDepthPreview = Data->bDepthPreview,
	};

	Context->UpdateSubresource(
		Renderer.ArrayPreviewCB.Get(),
		0,
		nullptr,
		&Constants,
		0,
		0);

	ID3D11Buffer* ConstantBuffer =
		Renderer.ArrayPreviewCB.Get();

	Context->PSSetConstantBuffers(
		0, 1, &ConstantBuffer);

	Context->PSSetShader(
		Renderer.ArrayPreviewPS.Get(),
		nullptr,
		0);

	Renderer.bArrayPreviewActive = true;
}

void FImGuiRenderer::RestoreArrayPreviewCallback(
	const ImDrawList*,
	const ImDrawCmd* DrawCmd)
{
	const auto* Data =
		static_cast<const FArrayPreviewCallbackData*>(
			DrawCmd->UserCallbackData);

	auto* RenderState =
		static_cast<ImGui_ImplDX11_RenderState*>(
			ImGui::GetPlatformIO().Renderer_RenderState);

	assert(Data && Data->Renderer);
	assert(RenderState && RenderState->DeviceContext);

	if (!Data || !Data->Renderer ||
		!RenderState || !RenderState->DeviceContext)
	{
		return;
	}

	FImGuiRenderer& Renderer = *Data->Renderer;

	if (!Renderer.bArrayPreviewActive)
	{
		return;
	}

	ID3D11Buffer* PreviousBuffer =
		Renderer.SavedPSConstantBuffer.Get();

	RenderState->DeviceContext->PSSetConstantBuffers(
		0, 1, &PreviousBuffer);

	Renderer.SavedPSConstantBuffer.Reset();
	Renderer.bArrayPreviewActive = false;
}

void FImGuiRenderer::ImageTextureArray(
	ID3D11ShaderResourceView* ArraySRV,
	uint32 ArrayIndex,
	const ImVec2& Size,
	const ImVec2& UV0,
	const ImVec2& UV1,
	bool bDepthPreview)
{
	if (!ArraySRV || !ArrayPreviewPS || !ArrayPreviewCB)
	{
		ImGui::Dummy(Size);
		return;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC ViewDesc{};
	ArraySRV->GetDesc(&ViewDesc);

	if (ViewDesc.ViewDimension !=
		D3D11_SRV_DIMENSION_TEXTURE2DARRAY)
	{
		ImGui::Dummy(Size);
		return;
	}

	// ArrayIndex는 이 SRV에 노출된 레이어 범위 기준이다.
	if (ArrayIndex >= ViewDesc.Texture2DArray.ArraySize)
	{
		ImGui::Dummy(Size);
		return;
	}

	ImDrawCallback ResetCallback =
		ImGui::GetPlatformIO().DrawCallback_ResetRenderState;

	if (!ResetCallback)
	{
		ImGui::Dummy(Size);
		return;
	}

	// 실제 UI 렌더링이 끝날 때까지 SRV를 유지한다.
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> KeepAlive;
	KeepAlive = ArraySRV;
	FramePreviewSRVs.Add(std::move(KeepAlive));

	FArrayPreviewCallbackData Data{
		.Renderer = this,
		.ArrayIndex = ArrayIndex,
		.bDepthPreview = bDepthPreview ? 1u : 0u,
	};

	ImDrawList* DrawList = ImGui::GetWindowDrawList();

	// sizeof(Data)를 전달하면 ImGui가 데이터를 복사한다.
	DrawList->AddCallback(
		&FImGuiRenderer::SetupArrayPreviewCallback,
		&Data,
		sizeof(Data));

	// 기존 백엔드가 ArraySRV를 PS t0에 바인딩하고 그린다.
	ImGui::Image(
		(ImTextureID)ArraySRV,
		Size,
		UV0,
		UV1);

	DrawList->AddCallback(
		&FImGuiRenderer::RestoreArrayPreviewCallback,
		&Data,
		sizeof(Data));

	DrawList->AddCallback(ResetCallback);
}
