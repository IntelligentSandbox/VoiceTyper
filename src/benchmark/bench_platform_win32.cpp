#include "host_services.h"

#define platform_audio_capture bench_real_platform_audio_capture
#define platform_get_foreground_window bench_real_platform_get_foreground_window
#include "platform_win32.h"
#undef platform_audio_capture
#undef platform_get_foreground_window

// The platform layer is header-only: the platform_ functions are inline
// definitions in platform_win32.h. Taking their address here forces this TU
// to emit them, so the benchmark executable - whose main TU only sees the
// declarations in host_services.h - links against them. platform_audio_capture
// and platform_get_foreground_window are renamed at include time (above) so
// the leak-soak mode can provide bench-side overrides of them in
// benchmark_main.cpp and still forward to the real implementations.
void *g_BenchPlatformAnchors[] = {
	(void *)&bench_real_platform_audio_capture,
	(void *)&bench_real_platform_get_foreground_window,
	(void *)&platform_query_audio_devices,
	(void *)&platform_inject_text,
	(void *)&platform_set_clipboard_text,
	(void *)&platform_get_window_process_name,
	(void *)&platform_set_taskbar_icon,
	(void *)&platform_apply_window_theme,
	(void *)&platform_play_sound,
	(void *)&platform_is_key_down,
	(void *)&platform_get_binary_path,
	(void *)&platform_get_binary_dir,
	(void *)&platform_get_data_dir,
	(void *)&platform_path_from_universal,
	(void *)&platform_ggml_backend_library_path,
	(void *)&platform_ensure_directory,
	(void *)&platform_remove_empty_directory,
	(void *)&platform_list_files,
	(void *)&platform_spawn_detached,
	(void *)&platform_is_installed_build,
	(void *)&platform_get_process_id,
	(void *)&platform_get_temp_dir,
	(void *)&platform_open_url,
	(void *)&platform_enumerate_fonts,
	(void *)&platform_init_crash_diagnostics,
	(void *)&platform_shutdown_crash_diagnostics,
	(void *)&platform_open_folder_selecting_file,
	(void *)&platform_download_file_thread,
	(void *)&platform_cancel_model_download,
	(void *)&platform_http_get_string,
	(void *)&platform_update_download_thread,
	(void *)&platform_cancel_update_download,
	(void *)&platform_apply_update_package,
	(void *)&platform_update_asset_tag,
	(void *)&platform_asset_is_installer,
};
