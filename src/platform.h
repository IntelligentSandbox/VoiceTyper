#pragma once

#include <vector>
#include <string>

struct GlobalState;
struct AudioInputDeviceInfo;

bool platform_audio_capture(GlobalState *AppState, int DeviceIndex);
std::vector<AudioInputDeviceInfo> platform_query_audio_devices();
void platform_inject_text(void *Window, const char *Utf8, bool CharByChar);
void *platform_get_foreground_window();
void platform_set_taskbar_icon(void *Window, const char *PngPath);
void platform_play_sound(int FreqHz, int DurationMs, int Volume);
bool platform_is_key_down(int VirtualKey);
std::string platform_get_exe_dir();

inline
std::string
platform_path_from_universal(const std::string &Path)
{
#ifdef _WIN32
	std::string Result = Path;
	for (char &Ch : Result)
	{
		if (Ch == '/')
			Ch = '\\';
	}
	return Result;
#else
	return Path;
#endif
}

inline
std::string
platform_join_path(const std::string &Base, const std::string &Relative)
{
	std::string Result = Base;
	for (char &Ch : Result)
	{
		if (Ch == '\\')
			Ch = '/';
	}

	if (!Result.empty() && Result.back() != '/')
		Result += '/';
	Result += Relative;

	return platform_path_from_universal(Result);
}
