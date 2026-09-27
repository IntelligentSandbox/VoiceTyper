#include <SDL.h>
#include <cstdio>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#include "build_time_constants.h"
#include "state.h"
#include "platform_linux.h"
#include "settings.h"
#include "app_core.h"
#include "imgui_ui.h"

static GlobalState *g_AppState = nullptr;

static Uint64 g_RenderIntervalTicks = 0;
static Uint64 g_RenderIdleIntervalTicks = 0;
static Uint64 g_LastInputCounter = 0;
static Uint64 g_LastPresentCounter = 0;
static bool g_RenderDueNow = true;

static int
detect_display_refresh_hz(SDL_Window *Window)
{
	int DisplayIndex = SDL_GetWindowDisplayIndex(Window);
	if (DisplayIndex < 0) return RENDER_REFRESH_FALLBACK_HZ;

	SDL_DisplayMode Mode = {};
	if (SDL_GetCurrentDisplayMode(DisplayIndex, &Mode) != 0) return RENDER_REFRESH_FALLBACK_HZ;
	if (Mode.refresh_rate <= 1 || Mode.refresh_rate > 1000) return RENDER_REFRESH_FALLBACK_HZ;

	return Mode.refresh_rate;
}

static bool
window_can_render(SDL_Window *Window)
{
	Uint32 Flags = SDL_GetWindowFlags(Window);
	return (Flags & SDL_WINDOW_SHOWN) != 0 && (Flags & SDL_WINDOW_MINIMIZED) == 0;
}

static bool
event_wakes_render(const SDL_Event &Event)
{
	switch (Event.type)
	{
	case SDL_MOUSEMOTION:
	case SDL_MOUSEBUTTONDOWN:
	case SDL_MOUSEBUTTONUP:
	case SDL_MOUSEWHEEL:
	case SDL_KEYDOWN:
	case SDL_KEYUP:
	case SDL_TEXTINPUT:
		return true;
	}

	return false;
}

static bool
ui_render_is_active(Uint64 Now, GlobalState *AppState)
{
	Uint64 IdleDelayTicks = SDL_GetPerformanceFrequency() * RENDER_IDLE_DELAY_MS / 1000;
	if (g_LastInputCounter != 0 && Now - g_LastInputCounter < IdleDelayTicks) return true;
	if (AppState && (AppState->IsRecording || AppState->IsStreaming)) return true;
	if (ImGui::GetIO().WantTextInput) return true;

	return false;
}

static bool
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

static void
save_window_size(SDL_Window *Window)
{
	int Width = 0;
	int Height = 0;
	SDL_GetWindowSize(Window, &Width, &Height);
	if (Width < WINDOW_MIN_WIDTH || Height < WINDOW_MIN_HEIGHT) return;

	save_window_size_setting(Width, Height);
	save_bool_setting("window_maximized", (SDL_GetWindowFlags(Window) & SDL_WINDOW_MAXIMIZED) != 0);

	// Wayland positions windows itself; the queried coordinates are not
	// meaningful there and must not overwrite a useful X11-saved position.
	const char *VideoDriver = SDL_GetCurrentVideoDriver();
	if (VideoDriver && std::string(VideoDriver) == "wayland") return;

	int X = 0;
	int Y = 0;
	SDL_GetWindowPosition(Window, &X, &Y);
	save_window_position_setting(X, Y);
}

static Uint64
performance_counter_now()
{
	return SDL_GetPerformanceCounter();
}

static Uint64
performance_interval_for_hz(int Hz)
{
	if (Hz <= 0) Hz = 60;

	Uint64 Ticks = SDL_GetPerformanceFrequency() / (Uint64)Hz;
	if (Ticks < 1) return 1;

	return Ticks;
}

static Uint32
milliseconds_until_counter(Uint64 Now, Uint64 Deadline)
{
	if (Deadline <= Now) return 0;

	Uint64 Frequency = SDL_GetPerformanceFrequency();
	Uint64 Ticks = Deadline - Now;
	Uint64 Milliseconds = (Ticks * 1000 + Frequency - 1) / Frequency;
	if (Milliseconds > 0x7fffffff) return 0x7fffffff;

	return (Uint32)Milliseconds;
}

static void
linux_load_font_atlas(GlobalState *AppState)
{
	ImGuiIO &Io = ImGui::GetIO();
	Io.Fonts->Clear();

	bool Loaded = false;
	if (!AppState->UiFontName.empty())
	{
		std::string Path;
		if (resolve_font_path(AppState->UiFontName, &Path))
		{
			Loaded = Io.Fonts->AddFontFromFileTTF(Path.c_str(), (float)AppState->UiFontSize) != nullptr;
		}
		if (!Loaded)
		{
			show_toast(AppState, "Configured font could not be loaded; using a system default.");
		}
	}

	if (!Loaded)
	{
		const char *FallbackNames[] = {
			"DejaVu Sans",
			"Liberation Sans",
			"Noto Sans",
			"FreeSans",
			"Ubuntu",
		};
		for (const char *Name : FallbackNames)
		{
			std::string Path;
			if (resolve_font_path(Name, &Path) &&
				Io.Fonts->AddFontFromFileTTF(Path.c_str(), (float)AppState->UiFontSize) != nullptr)
			{
				Loaded = true;
				break;
			}
		}
	}

	if (!Loaded)
	{
		Io.Fonts->AddFontDefault();
	}
}

static void
render_frame(SDL_Renderer *Renderer)
{
	if (!g_AppState) return;

	if (g_AppState->Ui.FontReloadRequested)
	{
		g_AppState->Ui.FontReloadRequested = false;
		linux_load_font_atlas(g_AppState);
	}

	int OutputW = 0;
	int OutputH = 0;
	SDL_GetRendererOutputSize(Renderer, &OutputW, &OutputH);
	if (OutputW <= 0 || OutputH <= 0) return;

	ImGuiIO &Io = ImGui::GetIO();
	ImGui_ImplSDLRenderer2_NewFrame();
	ImGui_ImplSDL2_NewFrame();
	ImGui::NewFrame();

	render_main_ui(g_AppState, Io);

	ImGui::Render();
	if (g_AppState->Ui.LightMode) SDL_SetRenderDrawColor(Renderer, 240, 240, 240, 255);
	else SDL_SetRenderDrawColor(Renderer, 25, 25, 25, 255);
	SDL_RenderClear(Renderer);
	ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), Renderer);
	SDL_RenderPresent(Renderer);
}

int
main(int, char **)
{
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
	{
		printf("[platform_linux] SDL init failed: %s\n", SDL_GetError());
		return 1;
	}

	if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
	{
		printf("[platform_linux] SDL audio init failed: %s\n", SDL_GetError());
	}

	SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");

	int WindowWidth = WINDOW_DEFAULT_WIDTH;
	int WindowHeight = WINDOW_DEFAULT_HEIGHT;
	bool HasSavedWindowSize = load_window_size(&WindowWidth, &WindowHeight);

	bool Maximized = true;
	if (HasSavedWindowSize) load_bool_setting("window_maximized", &Maximized);

	// Restore the exact last-closed position when the display server allows
	// client positioning (X11); on Wayland the compositor decides and the
	// coordinates are ignored.
	int WindowX = SDL_WINDOWPOS_CENTERED;
	int WindowY = SDL_WINDOWPOS_CENTERED;
	if (HasSavedWindowSize)
	{
		int SavedX = 0;
		int SavedY = 0;
		if (load_window_position_setting(&SavedX, &SavedY))
		{
			WindowX = SavedX;
			WindowY = SavedY;
		}
	}

	SDL_Window *Window = SDL_CreateWindow(
		"VoiceTyper",
		WindowX, WindowY,
		WindowWidth, WindowHeight,
		SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN);
	if (!Window)
	{
		printf("[platform_linux] SDL window creation failed: %s\n", SDL_GetError());
		SDL_Quit();
		return 1;
	}

	SDL_Renderer *Renderer = SDL_CreateRenderer(Window, -1, SDL_RENDERER_SOFTWARE);
	if (!Renderer)
	{
		printf("[platform_linux] SDL renderer creation failed: %s\n", SDL_GetError());
		SDL_DestroyWindow(Window);
		SDL_Quit();
		return 1;
	}

	GlobalState AppStateStorage = {};
	GlobalState *AppState = &AppStateStorage;
	g_AppState = AppState;

	app_initialize_runtime(AppState, Window);
	platform_set_taskbar_icon(Window, APP_ICON_PATH);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &Io = ImGui::GetIO();
	Io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	Io.IniFilename = nullptr;

	apply_ui_theme(AppState);
	linux_load_font_atlas(AppState);

	ImGui_ImplSDL2_InitForSDLRenderer(Window, Renderer);
	ImGui_ImplSDLRenderer2_Init(Renderer);

	if (Maximized)
	{
		SDL_MaximizeWindow(Window);

		// Let the WM/compositor apply the maximize while the window is still
		// hidden, so the first rendered frame uses the final window size.
		Uint32 HardDeadline = SDL_GetTicks() + 150;
		Uint32 LastSizeEvent = SDL_GetTicks();
		SDL_Event Event;
		while (SDL_GetTicks() - LastSizeEvent < 30 && SDL_GetTicks() < HardDeadline)
		{
			while (SDL_PollEvent(&Event))
			{
				ImGui_ImplSDL2_ProcessEvent(&Event);
				if (Event.type == SDL_WINDOWEVENT &&
					(Event.window.event == SDL_WINDOWEVENT_RESIZED ||
					 Event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED))
				{
					LastSizeEvent = SDL_GetTicks();
				}
			}
			SDL_Delay(5);
		}
	}

	// Render the first frame for the final window size before showing the
	// window, so the first thing presented on screen is the properly laid-out
	// UI instead of a blank or wrongly sized layout.
	render_frame(Renderer);
	SDL_ShowWindow(Window);

	// Kick the CUDA/GPU probe last: dlopening the CUDA plugin and its .so
	// closure holds the loader lock, so it must not overlap the UI thread's
	// init work above.
	refresh_inference_devices(AppState);

	const Uint64 AppUpdateIntervalTicks = performance_interval_for_hz(APP_UPDATE_HZ);
	g_RenderIdleIntervalTicks = performance_interval_for_hz(RENDER_IDLE_REFRESH_HZ);
	g_RenderIntervalTicks = performance_interval_for_hz(detect_display_refresh_hz(Window));
	Uint64 Now = performance_counter_now();
	Uint64 NextAppTick = Now;
	Uint64 NextRenderTick = Now;

	AppFrameState FrameState = {};

	bool Running = true;
	while (Running)
	{
		SDL_Event Event;
		while (SDL_PollEvent(&Event))
		{
			ImGui_ImplSDL2_ProcessEvent(&Event);
			if (event_wakes_render(Event))
			{
				g_LastInputCounter = performance_counter_now();
				g_RenderDueNow = true;
			}
			if (Event.type == SDL_QUIT) Running = false;
			if (Event.type != SDL_WINDOWEVENT) continue;
			if (Event.window.windowID != SDL_GetWindowID(Window)) continue;
			if (Event.window.event == SDL_WINDOWEVENT_CLOSE) Running = false;
			if (Event.window.event == SDL_WINDOWEVENT_RESIZED || Event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
			{
				save_window_size(Window);
			}
			if (Event.window.event == SDL_WINDOWEVENT_MOVED)
			{
				g_RenderIntervalTicks = performance_interval_for_hz(detect_display_refresh_hz(Window));
				g_RenderDueNow = true;
			}
		}

		if (AppState->ExitRequested.load()) Running = false;
		if (!Running) break;

		Now = performance_counter_now();

		int AppTicksRun = 0;
		while (Now >= NextAppTick && AppTicksRun < APP_UPDATE_MAX_CATCH_UP_TICKS)
		{
			AppFrameResult FrameResult = app_update_runtime_frame(
				AppState,
				&FrameState,
				!AppState->Ui.SettingsState.Capture.IsCapturing);
			show_model_transition_failure(AppState, FrameResult.ModelFailure);

			NextAppTick += AppUpdateIntervalTicks;
			AppTicksRun++;
			Now = performance_counter_now();
		}

		if (Now >= NextAppTick) NextAppTick = Now + AppUpdateIntervalTicks;

		// Input wakes a render instantly (capped at one refresh interval since
		// the last present); otherwise frames follow the explicit schedule,
		// which drops to RENDER_IDLE_REFRESH_HZ after RENDER_IDLE_DELAY_MS
		// without input/recording so the software renderer idles cheap.
		bool InputWake = g_RenderDueNow &&
			(g_LastPresentCounter == 0 || Now - g_LastPresentCounter >= g_RenderIntervalTicks);
		if (window_can_render(Window) && (Now >= NextRenderTick || InputWake))
		{
			Uint64 RenderStart = Now;

			render_frame(Renderer);
			Now = performance_counter_now();
			g_LastPresentCounter = Now;

			g_RenderDueNow = false;
			InputWake = false;
			Uint64 EffectiveRenderInterval = g_RenderIntervalTicks;
			if (g_RenderIdleIntervalTicks > EffectiveRenderInterval && !ui_render_is_active(Now, AppState))
			{
				EffectiveRenderInterval = g_RenderIdleIntervalTicks;
			}
			NextRenderTick = RenderStart + EffectiveRenderInterval;
		}

		Now = performance_counter_now();
		Uint64 NextDeadline = NextAppTick;
		if (window_can_render(Window))
		{
			if (InputWake)
			{
				NextDeadline = Now;
			}
			else if (NextRenderTick < NextDeadline)
			{
				NextDeadline = NextRenderTick;
			}
		}
		Uint32 WaitMs = milliseconds_until_counter(Now, NextDeadline);
		if (WaitMs > RENDER_SLEEP_MAX_MS) WaitMs = RENDER_SLEEP_MAX_MS;
		if (WaitMs > 0) SDL_Delay(WaitMs);
	}

	g_AppState = nullptr;
	app_shutdown_runtime(AppState);

	ImGui_ImplSDLRenderer2_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();

	SDL_DestroyRenderer(Renderer);
	SDL_DestroyWindow(Window);
	SDL_Quit();

	return 0;
}
