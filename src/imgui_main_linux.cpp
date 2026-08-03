#include <SDL.h>
#include <cstdio>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#include "state.h"
#include "platform_linux.h"
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

static GlobalState *g_AppState = nullptr;
static const int WINDOW_MIN_WIDTH = 320;
static const int WINDOW_MIN_HEIGHT = 240;
static const int RENDER_SLEEP_MAX_MS = 16;

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
render_frame(SDL_Renderer *Renderer)
{
	if (!g_AppState) return;

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
	SDL_SetRenderDrawColor(Renderer, 25, 25, 25, 255);
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
	load_window_size(&WindowWidth, &WindowHeight);

	SDL_Window *Window = SDL_CreateWindow(
		"VoiceTyper",
		SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED,
		WindowWidth,
		WindowHeight,
		SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
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

	ImGui::StyleColorsDark();
	Io.Fonts->AddFontDefault();

	ImGui_ImplSDL2_InitForSDLRenderer(Window, Renderer);
	ImGui_ImplSDLRenderer2_Init(Renderer);

	const Uint64 AppUpdateIntervalTicks = performance_interval_for_hz(VOICETYPER_APP_UPDATE_HZ);
	Uint64 Now = performance_counter_now();
	Uint64 NextAppTick = Now;

	AppFrameState FrameState = {};

	bool Running = true;
	while (Running)
	{
		SDL_Event Event;
		while (SDL_PollEvent(&Event))
		{
			ImGui_ImplSDL2_ProcessEvent(&Event);
			if (Event.type == SDL_QUIT) Running = false;
			if (Event.type != SDL_WINDOWEVENT) continue;
			if (Event.window.windowID != SDL_GetWindowID(Window)) continue;
			if (Event.window.event == SDL_WINDOWEVENT_CLOSE) Running = false;
			if (Event.window.event == SDL_WINDOWEVENT_RESIZED || Event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
			{
				save_window_size(Window);
			}
		}

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

		if (Now >= NextAppTick) NextAppTick = Now + AppUpdateIntervalTicks;

		render_frame(Renderer);

		Now = performance_counter_now();
		Uint32 WaitMs = milliseconds_until_counter(Now, NextAppTick);
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
