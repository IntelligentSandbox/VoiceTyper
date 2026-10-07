#pragma once

#include "runtime_types.h"

#include <vector>
#include <string>

struct GlobalState;

bool platform_audio_capture(PlatformRuntimeState *Platform, GlobalState *AppState, int DeviceIndex);
void platform_close_warm_audio_device();
std::string platform_path_from_universal(const std::string &Path);
std::string platform_ggml_backend_library_path(const std::string &SearchDir, const char *BackendName);
void platform_init_crash_diagnostics();
void platform_shutdown_crash_diagnostics();
void platform_open_folder_selecting_file(const std::string &FilePath);
void platform_download_file_thread(GlobalState *AppState, std::string Url, std::string DestPath, int64_t ExpectedSize);
void platform_cancel_model_download(GlobalState *AppState);
bool platform_http_get_string(const std::string &Url, std::string *OutBody,
	const std::string *IfNoneMatch = nullptr, std::string *OutEtag = nullptr,
	bool *OutNotModified = nullptr);
void platform_update_download_thread(GlobalState *AppState, std::string Url, std::string DestPath);
void platform_cancel_update_download(GlobalState *AppState);
bool platform_apply_update_package(const std::string &PackagePath);
const char *platform_update_asset_tag();
bool platform_asset_is_installer(const std::string &AssetName);
const char *platform_cuda_plugin_asset_tag();
void platform_cuda_plugin_download_run(GlobalState *AppState, const std::string &Url, const std::string &DestPath);
void platform_cancel_cuda_plugin_download(GlobalState *AppState);
bool platform_extract_archive(const std::string &ArchivePath, const std::string &DestDir);
std::vector<AudioInputDeviceInfo> platform_query_audio_devices();
void platform_inject_text(PlatformRuntimeState *Platform, void *Window, const char *Utf8, bool CharByChar, HotkeyConfig PasteHotkey, bool PreserveClipboard, int ClipboardRestoreDelayMs);
void platform_set_clipboard_text(PlatformRuntimeState *Platform, const char *Utf8);
void *platform_get_foreground_window(PlatformRuntimeState *Platform);
std::string platform_get_window_process_name(void *Window);
void platform_set_taskbar_icon(void *Window, const char *PngPath);
void platform_apply_window_theme(void *Window, bool LightMode);
void platform_play_sound(PlatformRuntimeState *Platform, int FreqHz, int DurationMs);
bool platform_is_key_down(AppKeyCode Key);
std::string platform_get_binary_path();
std::string platform_get_binary_dir();
std::string platform_get_data_dir();
bool platform_ensure_directory(const std::string &Path);
bool platform_remove_empty_directory(const std::string &Path);
std::vector<PlatformFileInfo> platform_list_files(const std::string &Dir);
bool platform_spawn_detached(const std::string &CommandLine, const std::string &WorkingDir, bool Hidden);
bool platform_is_installed_build();
int platform_get_process_id();
std::string platform_get_temp_dir();
void platform_open_url(const char *Url);
std::vector<PlatformFontInfo> platform_enumerate_fonts();

inline std::string
platform_join_path(const std::string &Base, const std::string &Relative)
{
	std::string Result = Base;
	for (char &Ch : Result)
	{
		if (Ch == '\\') Ch = '/';
	}

	if (!Result.empty() && Result.back() != '/') Result += '/';
	Result += Relative;

	return platform_path_from_universal(Result);
}
