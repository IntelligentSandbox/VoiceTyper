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

#ifndef VOICETYPER_APP_UPDATE_HZ
#define VOICETYPER_APP_UPDATE_HZ 100
#endif

#ifndef VOICETYPER_APP_UPDATE_MAX_CATCH_UP_TICKS
#define VOICETYPER_APP_UPDATE_MAX_CATCH_UP_TICKS 5
#endif

static_assert(VOICETYPER_APP_UPDATE_HZ > 0, "VOICETYPER_APP_UPDATE_HZ must be positive");
static_assert(
	VOICETYPER_APP_UPDATE_MAX_CATCH_UP_TICKS > 0,
	"VOICETYPER_APP_UPDATE_MAX_CATCH_UP_TICKS must be positive");

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
static bool                    g_RenderDueNow      = true;
static int                     g_RenderRefreshHz   = 60;
static LONGLONG                g_RenderIntervalTicks = 0;
static LONGLONG                g_PerformanceCounterFrequency = 0;
static const int               WINDOW_MIN_WIDTH    = 320;
static const int               WINDOW_MIN_HEIGHT   = 240;
static const int               RENDER_REFRESH_FALLBACK_HZ = 60;

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

static
LONGLONG
performance_counter_frequency()
{
	if (g_PerformanceCounterFrequency > 0) return g_PerformanceCounterFrequency;

	LARGE_INTEGER Frequency = {};
	if (QueryPerformanceFrequency(&Frequency) && Frequency.QuadPart > 0)
	{
		g_PerformanceCounterFrequency = Frequency.QuadPart;
	}
	else
	{
		g_PerformanceCounterFrequency = 1000;
	}

	return g_PerformanceCounterFrequency;
}

static
LONGLONG
performance_counter_now()
{
	LARGE_INTEGER Counter = {};
	QueryPerformanceCounter(&Counter);
	return Counter.QuadPart;
}

static
LONGLONG
performance_interval_for_hz(int Hz)
{
	if (Hz <= 0) Hz = RENDER_REFRESH_FALLBACK_HZ;

	LONGLONG Ticks = performance_counter_frequency() / Hz;
	if (Ticks < 1) return 1;

	return Ticks;
}

static
DWORD
milliseconds_until_counter(LONGLONG Now, LONGLONG Deadline)
{
	if (Deadline <= Now) return 0;

	LONGLONG Frequency = performance_counter_frequency();
	LONGLONG Ticks = Deadline - Now;
	LONGLONG Milliseconds = (Ticks * 1000 + Frequency - 1) / Frequency;
	if (Milliseconds > 0x7fffffff) return 0x7fffffff;

	return (DWORD)Milliseconds;
}

static
int
detect_monitor_refresh_hz(HWND Hwnd)
{
	HMONITOR Monitor = MonitorFromWindow(Hwnd, MONITOR_DEFAULTTONEAREST);
	if (!Monitor) return RENDER_REFRESH_FALLBACK_HZ;

	MONITORINFOEXW MonitorInfo = {};
	MonitorInfo.cbSize = sizeof(MonitorInfo);
	if (!GetMonitorInfoW(Monitor, &MonitorInfo)) return RENDER_REFRESH_FALLBACK_HZ;

	DEVMODEW DevMode = {};
	DevMode.dmSize = sizeof(DevMode);
	if (!EnumDisplaySettingsW(MonitorInfo.szDevice, ENUM_CURRENT_SETTINGS, &DevMode))
		return RENDER_REFRESH_FALLBACK_HZ;

	if (DevMode.dmDisplayFrequency <= 1 || DevMode.dmDisplayFrequency > 1000)
		return RENDER_REFRESH_FALLBACK_HZ;

	return (int)DevMode.dmDisplayFrequency;
}

static
void
refresh_render_cadence(HWND Hwnd)
{
	g_RenderRefreshHz = detect_monitor_refresh_hz(Hwnd);
	g_RenderIntervalTicks = performance_interval_for_hz(g_RenderRefreshHz);
	g_RenderDueNow = true;
}

static
bool
window_can_render(HWND Hwnd)
{
	return g_RenderTargetView && IsWindowVisible(Hwnd) && !IsIconic(Hwnd);
}

static
bool
swap_chain_is_still_occluded()
{
	if (!g_SwapChainOccluded) return false;
	if (!g_SwapChain) return true;

	HRESULT Hr = g_SwapChain->Present(0, DXGI_PRESENT_TEST);
	if (Hr == DXGI_STATUS_OCCLUDED) return true;

	g_SwapChainOccluded = false;
	return false;
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

	// TODO: Reevaluate this if vsync fights the explicit render scheduler or causes cadence issues.
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
		refresh_render_cadence(Hwnd);
		g_RenderDueNow = true;
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
		}
		return 0;

	case WM_MOVE:
		refresh_render_cadence(Hwnd);
		g_RenderDueNow = true;
		return 0;

	case WM_DISPLAYCHANGE:
		refresh_render_cadence(Hwnd);
		g_RenderDueNow = true;
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

	refresh_render_cadence(Hwnd);
	const LONGLONG AppUpdateIntervalTicks = performance_interval_for_hz(VOICETYPER_APP_UPDATE_HZ);
	LONGLONG Now = performance_counter_now();
	LONGLONG NextAppTick = Now;
	LONGLONG NextRenderTick = Now;

	AppFrameState FrameState = {};

	bool Running = true;
	while (Running)
	{
		MSG Msg;
		while (PeekMessage(&Msg, nullptr, 0, 0, PM_REMOVE))
		{
			if (Msg.message == WM_QUIT)
			{
				Running = false;
				break;
			}

			TranslateMessage(&Msg);
			DispatchMessage(&Msg);
		}

		if (!Running) break;

		Now = performance_counter_now();

		int AppTicksRun = 0;
		while (Now >= NextAppTick && AppTicksRun < VOICETYPER_APP_UPDATE_MAX_CATCH_UP_TICKS)
		{
			AppFrameResult FrameResult = app_update_runtime_frame(
				AppState,
				&FrameState,
				!AppState->Ui.IsSettingsDialogOpen);
			show_model_transition_failure(AppState, FrameResult.ModelFailure);

			NextAppTick += AppUpdateIntervalTicks;
			AppTicksRun++;
			Now = performance_counter_now();
		}

		if (Now >= NextAppTick)
		{
			NextAppTick = Now + AppUpdateIntervalTicks;
		}

		if (window_can_render(Hwnd) && (g_RenderDueNow || Now >= NextRenderTick))
		{
			LONGLONG RenderStart = Now;

			if (!swap_chain_is_still_occluded())
			{
				render_frame();
				Now = performance_counter_now();
			}

			g_RenderDueNow = false;
			NextRenderTick = RenderStart + g_RenderIntervalTicks;
		}

		Now = performance_counter_now();
		LONGLONG NextDeadline = NextAppTick;
		if (window_can_render(Hwnd))
		{
			if (g_RenderDueNow)
			{
				NextDeadline = Now;
			}
			else if (NextRenderTick < NextDeadline)
			{
				NextDeadline = NextRenderTick;
			}
		}

		DWORD WaitMs = milliseconds_until_counter(Now, NextDeadline);
		MsgWaitForMultipleObjectsEx(0, nullptr, WaitMs, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
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
