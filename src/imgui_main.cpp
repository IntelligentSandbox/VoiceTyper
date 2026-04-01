// VoiceTyper - ImGui + Win32 + DirectX11 Entry Point

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <cstdio>

#pragma comment(lib, "dwmapi.lib")

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include "state.h"
#include "system.h"
#include "imgui_ui.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

// ---------------------------------------------------------------------------
// D3D11 Globals
// ---------------------------------------------------------------------------
static ID3D11Device           *g_Device            = nullptr;
static ID3D11DeviceContext    *g_DeviceContext     = nullptr;
static IDXGISwapChain         *g_SwapChain         = nullptr;
static ID3D11RenderTargetView *g_RenderTargetView  = nullptr;
static bool                    g_SwapChainOccluded = false;
static GlobalState            *g_AppState          = nullptr;
static bool                    g_ImGuiReady        = false;

// ---------------------------------------------------------------------------
// Forward Declarations
// ---------------------------------------------------------------------------
static bool create_device_d3d(HWND Hwnd);
static void cleanup_device_d3d();
static void create_render_target();
static void cleanup_render_target();
LRESULT WINAPI wnd_proc(HWND Hwnd, UINT Msg, WPARAM WParam, LPARAM LParam);

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ---------------------------------------------------------------------------
// D3D11 Setup
// ---------------------------------------------------------------------------
static
bool
create_device_d3d(HWND Hwnd)
{
	DXGI_SWAP_CHAIN_DESC Sd = {};
	Sd.BufferCount = 2;
	Sd.BufferDesc.Width = 0;
	Sd.BufferDesc.Height = 0;
	Sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	Sd.BufferDesc.RefreshRate.Numerator = 60;
	Sd.BufferDesc.RefreshRate.Denominator = 1;
	Sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	Sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	Sd.OutputWindow = Hwnd;
	Sd.SampleDesc.Count = 1;
	Sd.SampleDesc.Quality = 0;
	Sd.Windowed = TRUE;
	Sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	UINT CreateFlags = 0;
#ifdef DEBUG
	CreateFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	D3D_FEATURE_LEVEL FeatureLevel;
	const D3D_FEATURE_LEVEL FeatureLevels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };

	HRESULT Hr = D3D11CreateDeviceAndSwapChain(
		nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, CreateFlags,
		FeatureLevels, 2, D3D11_SDK_VERSION,
		&Sd, &g_SwapChain, &g_Device, &FeatureLevel, &g_DeviceContext);

	if (Hr == DXGI_ERROR_UNSUPPORTED)
	{
		Hr = D3D11CreateDeviceAndSwapChain(
			nullptr, D3D_DRIVER_TYPE_WARP, nullptr, CreateFlags,
			FeatureLevels, 2, D3D11_SDK_VERSION,
			&Sd, &g_SwapChain, &g_Device, &FeatureLevel, &g_DeviceContext);
	}

	if (FAILED(Hr)) return false;

	create_render_target();
	return true;
}

static
void
cleanup_device_d3d()
{
	cleanup_render_target();
	if (g_SwapChain)      { g_SwapChain->Release();      g_SwapChain = nullptr; }
	if (g_DeviceContext)  { g_DeviceContext->Release();  g_DeviceContext = nullptr; }
	if (g_Device)         { g_Device->Release();         g_Device = nullptr; }
}

static
void
create_render_target()
{
	ID3D11Texture2D *BackBuffer = nullptr;
	g_SwapChain->GetBuffer(0, IID_PPV_ARGS(&BackBuffer));
	g_Device->CreateRenderTargetView(BackBuffer, nullptr, &g_RenderTargetView);
	BackBuffer->Release();
}

static
void
cleanup_render_target()
{
	if (g_RenderTargetView)
	{
		g_RenderTargetView->Release();
		g_RenderTargetView = nullptr;
	}
}

// ---------------------------------------------------------------------------
// Render a single frame
// ---------------------------------------------------------------------------
static
void
render_frame()
{
	if (!g_ImGuiReady || !g_AppState) return;

	ImGuiIO &Io = ImGui::GetIO();
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	render_main_ui(g_AppState, Io);

	ImGui::Render();
	const float ClearColor[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
	g_DeviceContext->OMSetRenderTargets(1, &g_RenderTargetView, nullptr);
	g_DeviceContext->ClearRenderTargetView(g_RenderTargetView, ClearColor);
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

	HRESULT Hr = g_SwapChain->Present(1, 0);
	g_SwapChainOccluded = (Hr == DXGI_STATUS_OCCLUDED);
}

// ---------------------------------------------------------------------------
// Window Procedure
// ---------------------------------------------------------------------------
LRESULT WINAPI
wnd_proc(HWND Hwnd, UINT Msg, WPARAM WParam, LPARAM LParam)
{
	if (ImGui_ImplWin32_WndProcHandler(Hwnd, Msg, WParam, LParam))
		return 1;

	switch (Msg)
	{
	case WM_SIZE:
		if (WParam == SIZE_MINIMIZED) return 0;
		cleanup_render_target();
		g_SwapChain->ResizeBuffers(0, (UINT)LOWORD(LParam), (UINT)HIWORD(LParam), DXGI_FORMAT_UNKNOWN, 0);
		create_render_target();
		render_frame();
		return 0;

	case WM_SYSCOMMAND:
		if ((WParam & 0xfff0) == SC_KEYMENU) return 0;
		break;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProcW(Hwnd, Msg, WParam, LParam);
}

// ---------------------------------------------------------------------------
// Main Entry Point
// ---------------------------------------------------------------------------
int WINAPI
WinMain(HINSTANCE Instance, HINSTANCE /*PrevInstance*/, LPSTR /*CmdLine*/, int /*ShowCmd*/)
{
#if defined(DEBUG) && defined(_WIN32)
	if (!AttachConsole(ATTACH_PARENT_PROCESS)) AllocConsole();
	freopen("CONOUT$", "w", stdout);
	freopen("CONOUT$", "w", stderr);
#endif

	ImGui_ImplWin32_EnableDpiAwareness();

	HICON AppIcon = LoadIconW(Instance, MAKEINTRESOURCEW(101));

	WNDCLASSEXW Wc = {};
	Wc.cbSize = sizeof(Wc);
	Wc.style = CS_CLASSDC;
	Wc.lpfnWndProc = wnd_proc;
	Wc.hInstance = Instance;
	Wc.lpszClassName = L"VoiceTyperClass";
	Wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	Wc.hIcon = AppIcon;
	Wc.hIconSm = AppIcon;
	RegisterClassExW(&Wc);

	HWND Hwnd = CreateWindowW(
		Wc.lpszClassName, L"VoiceTyper", WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT,
		WINDOW_DEFAULT_WIDTH, WINDOW_DEFAULT_HEIGHT,
		nullptr, nullptr, Instance, nullptr);

	if (!Hwnd) return 1;

	BOOL DarkMode = TRUE;
	DwmSetWindowAttribute(Hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &DarkMode, sizeof(DarkMode));
	COLORREF CaptionColor = RGB(26, 26, 26);
	DwmSetWindowAttribute(Hwnd, DWMWA_CAPTION_COLOR, &CaptionColor, sizeof(CaptionColor));

	if (!create_device_d3d(Hwnd))
	{
		cleanup_device_d3d();
		UnregisterClassW(Wc.lpszClassName, Instance);
		return 1;
	}

	ShowWindow(Hwnd, SW_SHOWDEFAULT);
	UpdateWindow(Hwnd);

	GlobalState AppStateStorage = {};
	AppStateStorage.IsRecording            = false;
	AppStateStorage.IsStreaming            = false;
	AppStateStorage.CaptureRunning        = false;
	AppStateStorage.OwnWindow              = Hwnd;
	AppStateStorage.IsSettingsDialogOpen   = false;
	AppStateStorage.PlayRecordSound        = false;
	AppStateStorage.UseCharByCharInjection = false;
	GlobalState *AppState = &AppStateStorage;
	g_AppState = AppState;

	init_whisper_state(&AppState->WhisperState);

	query_audio_input_devices(AppState);
	query_inference_devices(AppState);
	query_available_stt_models(AppState);
	query_whisper_thread_count(AppState);
	query_hotkey_settings(AppState);

	platform_set_taskbar_icon((void*)Hwnd, APP_ICON_PATH);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &Io = ImGui::GetIO();
	Io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	Io.IniFilename = nullptr;

	ImGui::StyleColorsDark();

	Io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\georgia.ttf", 18.0f);

	ImGui_ImplWin32_Init(Hwnd);
	ImGui_ImplDX11_Init(g_Device, g_DeviceContext);
	g_ImGuiReady = true;

	bool RecordKeyWasDown       = false;
	bool CancelRecordKeyWasDown = false;
	bool StreamKeyWasDown       = false;
	bool LoadModelKeyWasDown    = false;

	bool Running = true;
	while (Running)
	{
		MSG Msg;
		while (PeekMessage(&Msg, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&Msg);
			DispatchMessage(&Msg);
			if (Msg.message == WM_QUIT) Running = false;
		}

		if (!Running) break;

		if (g_SwapChainOccluded && g_SwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
		{
			Sleep(10);
			continue;
		}
		g_SwapChainOccluded = false;

		if (!AppState->IsSettingsDialogOpen)
		{
			bool RecordKeyIsDown       = is_hotkey_down(AppState->RecordHotkey);
			bool CancelRecordKeyIsDown = is_hotkey_down(AppState->CancelRecordHotkey);
			bool StreamKeyIsDown       = is_hotkey_down(AppState->StreamHotkey);
			bool LoadModelKeyIsDown    = is_hotkey_down(AppState->LoadModelHotkey);

			if (RecordKeyIsDown && !RecordKeyWasDown)
				toggle_recording(AppState);

			if (CancelRecordKeyIsDown && !CancelRecordKeyWasDown)
				cancel_recording(AppState);

			if (StreamKeyIsDown && !StreamKeyWasDown)
				toggle_streaming(AppState);

			if (LoadModelKeyIsDown && !LoadModelKeyWasDown)
				toggle_stt_model_load(AppState);

			RecordKeyWasDown       = RecordKeyIsDown;
			CancelRecordKeyWasDown = CancelRecordKeyIsDown;
			StreamKeyWasDown       = StreamKeyIsDown;
			LoadModelKeyWasDown    = LoadModelKeyIsDown;
		}

		render_frame();
	}

	g_ImGuiReady = false;

	if (AppState->IsRecording)
		AppState->CaptureRunning.store(false);

	if (AppState->IsStreaming)
	{
		AppState->CaptureRunning.store(false);
		if (AppState->CaptureThread.joinable())
			AppState->CaptureThread.join();
	}

	if (is_whisper_model_loaded(&AppState->WhisperState))
		unload_whisper_model(&AppState->WhisperState);

	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	cleanup_device_d3d();
	DestroyWindow(Hwnd);
	UnregisterClassW(Wc.lpszClassName, Instance);

	return 0;
}
