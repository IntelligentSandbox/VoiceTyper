#pragma once

#include "host_services.h"
#include "state.h"

#include <SDL.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <limits.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

inline std::vector<AudioInputDeviceInfo>
platform_query_audio_devices()
{
	std::vector<AudioInputDeviceInfo> Devices;

	if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0 && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
	{
		printf("[platform_linux] SDL audio init failed: %s\n", SDL_GetError());
		return Devices;
	}

	AudioInputDeviceInfo DefaultInfo = {};
	DefaultInfo.Index = 0;
	DefaultInfo.Id = "default";
	DefaultInfo.Name = "Default audio input";
	DefaultInfo.IsDefault = true;
	Devices.push_back(DefaultInfo);

	int NumDevices = SDL_GetNumAudioDevices(SDL_TRUE);
	for (int i = 0; i < NumDevices; i++)
	{
		const char *DeviceName = SDL_GetAudioDeviceName(i, SDL_TRUE);
		if (!DeviceName) continue;

		AudioInputDeviceInfo Info = {};
		Info.Index = i;
		Info.Id = std::to_string(i);
		Info.Name = DeviceName;
		Info.IsDefault = false;
		Devices.push_back(Info);
	}

	return Devices;
}

inline void
platform_inject_text(PlatformRuntimeState *Platform, void *Window, const char *Utf8, bool CharByChar)
{
	(void)Platform;
	(void)Window;
	(void)CharByChar;
	if (!Utf8 || Utf8[0] == '\0') return;

	SDL_SetClipboardText(Utf8);
}

inline void
platform_set_clipboard_text(PlatformRuntimeState *Platform, const char *Utf8)
{
	(void)Platform;
	if (!Utf8 || Utf8[0] == '\0') return;

	SDL_SetClipboardText(Utf8);
}

inline void *
platform_get_foreground_window(PlatformRuntimeState *Platform)
{
	(void)Platform;
	return nullptr;
}

inline void
platform_set_taskbar_icon(void *Window, const char *PngPath)
{
	(void)Window;
	(void)PngPath;
}

inline void
platform_play_sound(PlatformRuntimeState *Platform, int FreqHz, int DurationMs)
{
	(void)Platform;
	(void)FreqHz;
	(void)DurationMs;
}

static bool
platform_is_sdl_scancode_down(SDL_Scancode Scan)
{
	int NumKeys = 0;
	const Uint8 *Keys = SDL_GetKeyboardState(&NumKeys);
	if (!Keys || Scan == SDL_SCANCODE_UNKNOWN || Scan >= NumKeys) return false;

	return Keys[Scan] != 0;
}

inline bool
platform_is_key_down(AppKeyCode Key)
{
	if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0) return false;

	if (Key == APP_KEY_SHIFT)
	{
		return platform_is_sdl_scancode_down(SDL_SCANCODE_LSHIFT) ||
			platform_is_sdl_scancode_down(SDL_SCANCODE_RSHIFT);
	}
	if (Key == APP_KEY_CONTROL)
	{
		return platform_is_sdl_scancode_down(SDL_SCANCODE_LCTRL) ||
			platform_is_sdl_scancode_down(SDL_SCANCODE_RCTRL);
	}
	if (Key == APP_KEY_ALT)
	{
		return platform_is_sdl_scancode_down(SDL_SCANCODE_LALT) ||
			platform_is_sdl_scancode_down(SDL_SCANCODE_RALT);
	}
	if (Key == APP_KEY_WIN)
	{
		return platform_is_sdl_scancode_down(SDL_SCANCODE_LGUI) ||
			platform_is_sdl_scancode_down(SDL_SCANCODE_RGUI);
	}

	SDL_Scancode Scan = SDL_SCANCODE_UNKNOWN;
	if (Key >= 'A' && Key <= 'Z') Scan = (SDL_Scancode)(SDL_SCANCODE_A + (Key - 'A'));
	else if (Key >= APP_KEY_F1 && Key <= APP_KEY_F24) Scan = (SDL_Scancode)(SDL_SCANCODE_F1 + (Key - APP_KEY_F1));
	else
	{
		switch (Key)
		{
		case '0': Scan = SDL_SCANCODE_0; break;
		case '1': Scan = SDL_SCANCODE_1; break;
		case '2': Scan = SDL_SCANCODE_2; break;
		case '3': Scan = SDL_SCANCODE_3; break;
		case '4': Scan = SDL_SCANCODE_4; break;
		case '5': Scan = SDL_SCANCODE_5; break;
		case '6': Scan = SDL_SCANCODE_6; break;
		case '7': Scan = SDL_SCANCODE_7; break;
		case '8': Scan = SDL_SCANCODE_8; break;
		case '9': Scan = SDL_SCANCODE_9; break;
		case APP_KEY_SPACE:     Scan = SDL_SCANCODE_SPACE; break;
		case APP_KEY_ENTER:     Scan = SDL_SCANCODE_RETURN; break;
		case APP_KEY_ESCAPE:    Scan = SDL_SCANCODE_ESCAPE; break;
		case APP_KEY_TAB:       Scan = SDL_SCANCODE_TAB; break;
		case APP_KEY_BACKSPACE: Scan = SDL_SCANCODE_BACKSPACE; break;
		case APP_KEY_DELETE:    Scan = SDL_SCANCODE_DELETE; break;
		case APP_KEY_INSERT:    Scan = SDL_SCANCODE_INSERT; break;
		case APP_KEY_HOME:      Scan = SDL_SCANCODE_HOME; break;
		case APP_KEY_END:       Scan = SDL_SCANCODE_END; break;
		case APP_KEY_PAGEUP:    Scan = SDL_SCANCODE_PAGEUP; break;
		case APP_KEY_PAGEDOWN:  Scan = SDL_SCANCODE_PAGEDOWN; break;
		case APP_KEY_LEFT:      Scan = SDL_SCANCODE_LEFT; break;
		case APP_KEY_RIGHT:     Scan = SDL_SCANCODE_RIGHT; break;
		case APP_KEY_UP:        Scan = SDL_SCANCODE_UP; break;
		case APP_KEY_DOWN:      Scan = SDL_SCANCODE_DOWN; break;
		default: break;
		}
	}

	return platform_is_sdl_scancode_down(Scan);
}

inline std::string
platform_get_exe_path()
{
	char ExePath[PATH_MAX] = {};
	ssize_t Len = readlink("/proc/self/exe", ExePath, sizeof(ExePath) - 1);
	if (Len <= 0) return "";

	ExePath[Len] = '\0';
	return std::string(ExePath);
}

inline std::string
platform_get_exe_dir()
{
	const char *DataDir = getenv("VOICETYPER_DATA_DIR");
	if (DataDir && DataDir[0] != '\0') return std::string(DataDir);

	std::string ExePath = platform_get_exe_path();
	size_t LastSlash = ExePath.find_last_of('/');
	if (LastSlash != std::string::npos) ExePath.resize(LastSlash);
	return ExePath;
}

inline bool
platform_ensure_directory(const std::string &Path)
{
	if (Path.empty()) return false;

	std::string Normalized = Path;
	for (char &Ch : Normalized)
	{
		if (Ch == '\\') Ch = '/';
	}

	if (Normalized == "/") return true;

	size_t Pos = (Normalized[0] == '/') ? 1 : 0;
	for (;;)
	{
		Pos = Normalized.find('/', Pos);
		std::string Partial = (Pos == std::string::npos) ? Normalized : Normalized.substr(0, Pos);

		if (!Partial.empty() && mkdir(Partial.c_str(), 0755) != 0 && errno != EEXIST) return false;

		if (Pos == std::string::npos) break;
		Pos++;
	}

	return true;
}

inline bool
platform_remove_directory(const std::string &Path)
{
	if (Path.empty()) return false;
	return rmdir(Path.c_str()) == 0;
}
{
	std::vector<PlatformFileInfo> Files;
	DIR *Directory = opendir(Dir.c_str());
	if (!Directory) return Files;

	for (;;)
	{
		dirent *Entry = readdir(Directory);
		if (!Entry) break;
		if (strcmp(Entry->d_name, ".") == 0 || strcmp(Entry->d_name, "..") == 0) continue;

		std::string FilePath = platform_join_path(Dir, Entry->d_name);
		struct stat Stat = {};
		if (stat(FilePath.c_str(), &Stat) != 0) continue;
		if (S_ISDIR(Stat.st_mode)) continue;

		PlatformFileInfo Info = {};
		Info.Name = Entry->d_name;
		Info.SizeBytes = Stat.st_size;
		Files.push_back(Info);
	}

	closedir(Directory);
	return Files;
}

struct SdlCaptureContext
{
	GlobalState *AppState;
};

static void
platform_sdl_capture_callback(void *UserData, Uint8 *Stream, int Len)
{
	SdlCaptureContext *Context = (SdlCaptureContext *)UserData;
	if (!Context || !Context->AppState || !Stream || Len <= 0) return;

	int SampleCount = Len / (int)sizeof(int16_t);
	const int16_t *Samples = (const int16_t *)Stream;

	std::lock_guard<std::mutex> Lock(Context->AppState->AudioBufferMutex);
	size_t OldSize = Context->AppState->AudioAccumBuffer.size();
	Context->AppState->AudioAccumBuffer.resize(OldSize + SampleCount);
	for (int i = 0; i < SampleCount; i++)
	{
		Context->AppState->AudioAccumBuffer[OldSize + i] = Samples[i] / 32768.0f;
	}
}

inline bool
platform_audio_capture(PlatformRuntimeState *Platform, GlobalState *AppState, int DeviceIndex)
{
	(void)Platform;
	if (!AppState || DeviceIndex < 0 || DeviceIndex >= (int)AppState->AudioInputDevices.size()) return false;

	if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0 && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
	{
		printf("[audio_pipeline] ERROR: SDL audio init failed: %s\n", SDL_GetError());
		return false;
	}

	const AudioInputDeviceInfo &DeviceInfo = AppState->AudioInputDevices[DeviceIndex];
	const char *DeviceName = nullptr;
	if (DeviceInfo.Id != "default") DeviceName = DeviceInfo.Name.c_str();

	SdlCaptureContext Context = {};
	Context.AppState = AppState;

	SDL_AudioSpec Desired = {};
	Desired.freq = AUDIO_CAPTURE_SAMPLE_RATE;
	Desired.format = AUDIO_S16SYS;
	Desired.channels = AUDIO_CAPTURE_CHANNELS;
	Desired.samples = (AUDIO_CAPTURE_SAMPLE_RATE * AUDIO_CAPTURE_BUFFER_MS) / 1000;
	Desired.callback = platform_sdl_capture_callback;
	Desired.userdata = &Context;

	SDL_AudioSpec Obtained = {};
	SDL_AudioDeviceID Device = SDL_OpenAudioDevice(DeviceName, SDL_TRUE, &Desired, &Obtained, 0);
	if (Device == 0)
	{
		printf("[audio_pipeline] ERROR: SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
		return false;
	}

	SDL_PauseAudioDevice(Device, 0);
	while (AppState->CaptureRunning.load()) SDL_Delay(20);
	SDL_PauseAudioDevice(Device, 1);
	SDL_CloseAudioDevice(Device);

	return true;
}
