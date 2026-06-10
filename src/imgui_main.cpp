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
#include "platform_win32.h"
#include "settings.h"
#include "app_core.h"
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
static const int               WINDOW_MIN_WIDTH    = 320;
static const int               WINDOW_MIN_HEIGHT   = 240;

// ---------------------------------------------------------------------------
// Forward Declarations
// ---------------------------------------------------------------------------
static bool create_device_d3d(HWND Hwnd);
static void cleanup_device_d3d();
static void create_render_target();
static void cleanup_render_target();
static bool load_window_size(int *OutWidth, int *OutHeight);
static void save_window_size(HWND Hwnd);
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

static
bool
load_window_size(int *OutWidth, int *OutHeight)
{
	int Width = 0;
	int Height = 0;
	if (!load_window_size_setting(&Width, &Height)) return false;
	if (Width < WINDOW_MIN_WIDTH || Height < WINDOW_MIN_HEIGHT) return false;

	*OutWidth = Width;
	*OutHeight = Height;
	return true;
}

static
void
save_window_size(HWND Hwnd)
{
	RECT WindowRect = {};
	if (!GetWindowRect(Hwnd, &WindowRect)) return;

	int Width = WindowRect.right - WindowRect.left;
	int Height = WindowRect.bottom - WindowRect.top;
	if (Width < WINDOW_MIN_WIDTH || Height < WINDOW_MIN_HEIGHT) return;

	save_window_size_setting(Width, Height);
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
		if (WParam == SIZE_RESTORED)
		{
			save_window_size(Hwnd);
		}
		if (g_SwapChain)
		{
			cleanup_render_target();
			g_SwapChain->ResizeBuffers(
				0, (UINT)LOWORD(LParam), (UINT)HIWORD(LParam), DXGI_FORMAT_UNKNOWN, 0);
			create_render_target();
			render_frame();
		}
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

	int WindowWidth = WINDOW_DEFAULT_WIDTH;
	int WindowHeight = WINDOW_DEFAULT_HEIGHT;
	load_window_size(&WindowWidth, &WindowHeight);

	HWND Hwnd = CreateWindowW(
		Wc.lpszClassName, L"VoiceTyper", WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT,
		WindowWidth, WindowHeight,
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
	GlobalState *AppState = &AppStateStorage;
	g_AppState = AppState;

	app_initialize_runtime(AppState, Hwnd);

	platform_set_taskbar_icon((void*)Hwnd, APP_ICON_PATH);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &Io = ImGui::GetIO();
	Io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	Io.IniFilename = nullptr;

	ImGui::StyleColorsDark();

	std::string FontPath = platform_path_from_universal("C:/Windows/Fonts/georgia.ttf");
	Io.Fonts->AddFontFromFileTTF(FontPath.c_str(), 18.0f);

	ImGui_ImplWin32_Init(Hwnd);
	ImGui_ImplDX11_Init(g_Device, g_DeviceContext);
	g_ImGuiReady = true;

	AppFrameState FrameState = {};

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

		AppFrameResult FrameResult = app_update_runtime_frame(
			AppState,
			&FrameState,
			!AppState->Ui.IsSettingsDialogOpen);
		show_model_transition_failure(AppState, FrameResult.ModelFailure);

		render_frame();
	}

	g_ImGuiReady = false;

	app_shutdown_runtime(AppState);

	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	cleanup_device_d3d();
	DestroyWindow(Hwnd);
	UnregisterClassW(Wc.lpszClassName, Instance);

	return 0;
}
