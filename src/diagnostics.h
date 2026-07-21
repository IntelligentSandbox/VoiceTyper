#pragma once

#include "host_services.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <vector>

#ifdef _WIN32
	#include <windows.h>
	#include <shellapi.h>
	#include <dbghelp.h>
	#pragma comment(lib, "dbghelp.lib")
#endif

#include "whisper.h"
#include "ggml.h"

#define VOICETYPER_LOG_FILENAME         "debug.log"
#define VOICETYPER_CRASH_DUMP_PREFIX    "voicetyper-crash-"
#define VOICETYPER_CRASH_DUMP_SUFFIX    ".dmp"
#define VOICETYPER_CRASH_SEEN_SUFFIX    ".seen"
#define VOICETYPER_VERBOSE_ENV          "VOICETYPER_VERBOSE"

struct DiagnosticsState
{
	FILE        *LogFile;
	std::mutex   LogMutex;
	bool         Verbose;
#ifdef _WIN32
	LPTOP_LEVEL_EXCEPTION_FILTER PreviousExceptionFilter;
#endif
};

static DiagnosticsState g_Diagnostics = {};

// ---------------------------------------------------------------------------
// Log file
// ---------------------------------------------------------------------------

inline std::string
diag_log_file_path()
{
	return platform_join_path(platform_get_exe_dir(), VOICETYPER_LOG_FILENAME);
}

inline void
diag_write_log_line(ggml_log_level Level, const char *Message)
{
	if (!g_Diagnostics.LogFile) return;
	if (!g_Diagnostics.Verbose && Level < GGML_LOG_LEVEL_WARN) return;
	if (!Message || Message[0] == '\0') return;

	std::lock_guard<std::mutex> Lock(g_Diagnostics.LogMutex);
	fputs(Message, g_Diagnostics.LogFile);
	if (Level >= GGML_LOG_LEVEL_WARN) fflush(g_Diagnostics.LogFile);
}

static void
diag_ggml_log_callback(ggml_log_level Level, const char *Message, void *)
{
	diag_write_log_line(Level, Message);
}

// ---------------------------------------------------------------------------
// Crash dump (Win32)
// ---------------------------------------------------------------------------

#ifdef _WIN32

inline std::string
diag_crash_dump_path_for_now()
{
	time_t Now = time(nullptr);
	tm LocalTm = {};
	localtime_s(&LocalTm, &Now);

	char TimeBuf[32];
	strftime(TimeBuf, sizeof(TimeBuf), "%Y%m%d-%H%M%S", &LocalTm);

	std::string Filename = VOICETYPER_CRASH_DUMP_PREFIX;
	Filename += TimeBuf;
	Filename += VOICETYPER_CRASH_DUMP_SUFFIX;

	return platform_join_path(platform_get_exe_dir(), Filename);
}

inline void
diag_write_minidump(EXCEPTION_POINTERS *ExceptionInfo)
{
	std::string DumpPath = diag_crash_dump_path_for_now();

	HANDLE File = CreateFileA(DumpPath.c_str(), GENERIC_WRITE, 0, nullptr,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (File == INVALID_HANDLE_VALUE) return;

	MINIDUMP_EXCEPTION_INFORMATION Mei = {};
	Mei.ThreadId          = GetCurrentThreadId();
	Mei.ExceptionPointers = ExceptionInfo;
	Mei.ClientPointers    = FALSE;

	BOOL Ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), File,
		MiniDumpNormal, &Mei, nullptr, nullptr);

	CloseHandle(File);

	if (!Ok)
	{
		char Buf[128];
		snprintf(Buf, sizeof(Buf), "[diagnostics] MiniDumpWriteDump failed (err=%lu)\n",
			GetLastError());
		diag_write_log_line(GGML_LOG_LEVEL_ERROR, Buf);
		return;
	}

	char Buf[256];
	snprintf(Buf, sizeof(Buf),
		"[diagnostics] unhandled exception code=0x%08X addr=%p, minidump written to %s\n",
		(unsigned int)ExceptionInfo->ExceptionRecord->ExceptionCode,
		ExceptionInfo->ExceptionRecord->ExceptionAddress,
		DumpPath.c_str());
	diag_write_log_line(GGML_LOG_LEVEL_ERROR, Buf);
}

static LONG WINAPI
diag_unhandled_exception_filter(EXCEPTION_POINTERS *ExceptionInfo)
{
	diag_write_minidump(ExceptionInfo);

	if (g_Diagnostics.PreviousExceptionFilter)
		return g_Diagnostics.PreviousExceptionFilter(ExceptionInfo);

	return EXCEPTION_EXECUTE_HANDLER;
}

inline void
platform_open_folder_selecting_file(const std::string &FilePath)
{
	if (FilePath.empty()) return;

	int WideLen = MultiByteToWideChar(CP_UTF8, 0, FilePath.c_str(), -1, nullptr, 0);
	if (WideLen <= 0) return;

	std::wstring Wide(WideLen, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, FilePath.c_str(), -1, &Wide[0], WideLen);

	std::wstring Args = L"/select,\"" + Wide + L"\"";

	SHELLEXECUTEINFOW Sei = {};
	Sei.cbSize = sizeof(Sei);
	Sei.lpVerb       = L"open";
	Sei.lpFile       = L"explorer.exe";
	Sei.lpParameters = Args.c_str();
	Sei.nShow        = SW_SHOWNORMAL;
	ShellExecuteExW(&Sei);
}

#else

inline void
platform_open_folder_selecting_file(const std::string &)
{
	// Non-Win32 ports: no-op for now.
}

#endif

// ---------------------------------------------------------------------------
// Crash dump discovery (cross-platform)
// ---------------------------------------------------------------------------

inline bool
diag_has_seen_sidecar(const std::string &DumpPath)
{
	std::string SeenPath = DumpPath + VOICETYPER_CRASH_SEEN_SUFFIX;
	FILE *F = fopen(SeenPath.c_str(), "r");
	if (!F) return false;
	fclose(F);
	return true;
}

inline void
check_for_previous_crash_dumps(std::vector<std::string> *OutPaths)
{
	OutPaths->clear();

	std::string Dir = platform_get_exe_dir();
	std::vector<PlatformFileInfo> Files = platform_list_files(Dir);

	size_t SuffixLen = strlen(VOICETYPER_CRASH_DUMP_SUFFIX);

	for (const PlatformFileInfo &File : Files)
	{
		if (File.Name.rfind(VOICETYPER_CRASH_DUMP_PREFIX, 0) != 0) continue;

		size_t SuffixPos = File.Name.rfind(VOICETYPER_CRASH_DUMP_SUFFIX);
		if (SuffixPos == std::string::npos) continue;
		if (SuffixPos + SuffixLen != File.Name.size()) continue;

		std::string FullPath = platform_join_path(Dir, File.Name);
		if (diag_has_seen_sidecar(FullPath)) continue;

		OutPaths->push_back(FullPath);
	}
}

inline void
mark_crash_dump_seen(const std::string &DumpPath)
{
	std::string SeenPath = DumpPath + VOICETYPER_CRASH_SEEN_SUFFIX;
	FILE *F = fopen(SeenPath.c_str(), "w");
	if (!F) return;
	fclose(F);
}

// ---------------------------------------------------------------------------
// Init / shutdown
// ---------------------------------------------------------------------------

inline void
init_diagnostics()
{
	const char *VerboseEnv = std::getenv(VOICETYPER_VERBOSE_ENV);
	g_Diagnostics.Verbose = (VerboseEnv != nullptr && VerboseEnv[0] == '1');
	g_Diagnostics.LogFile = nullptr;

	std::string LogPath = diag_log_file_path();
	g_Diagnostics.LogFile = fopen(LogPath.c_str(), "a");

	if (g_Diagnostics.LogFile)
	{
		time_t Now = time(nullptr);
		tm LocalTm = {};
#ifdef _WIN32
		localtime_s(&LocalTm, &Now);
#else
		localtime_r(&Now, &LocalTm);
#endif
		char TimeBuf[32];
		strftime(TimeBuf, sizeof(TimeBuf), "%Y-%m-%d %H:%M:%S", &LocalTm);

		char Header[256];
		snprintf(Header, sizeof(Header),
			"\n=== VoiceTyper v%s session start %s (verbose=%s) ===\n",
			VOICETYPER_VERSION_FULL, TimeBuf,
			g_Diagnostics.Verbose ? "on" : "off");
		fputs(Header, g_Diagnostics.LogFile);
		fflush(g_Diagnostics.LogFile);
	}

	whisper_log_set(diag_ggml_log_callback, nullptr);
	ggml_log_set(diag_ggml_log_callback, nullptr);

#ifdef _WIN32
	g_Diagnostics.PreviousExceptionFilter = SetUnhandledExceptionFilter(diag_unhandled_exception_filter);
#endif
}

inline void
shutdown_diagnostics()
{
#ifdef _WIN32
	SetUnhandledExceptionFilter(g_Diagnostics.PreviousExceptionFilter);
	g_Diagnostics.PreviousExceptionFilter = nullptr;
#endif

	if (g_Diagnostics.LogFile)
	{
		std::lock_guard<std::mutex> Lock(g_Diagnostics.LogMutex);
		fflush(g_Diagnostics.LogFile);
		fclose(g_Diagnostics.LogFile);
		g_Diagnostics.LogFile = nullptr;
	}
}
