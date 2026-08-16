#pragma once

#include "host_services.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#ifdef _WIN32
	#include <windows.h>
	#include <shellapi.h>
	#include <dbghelp.h>
	#pragma comment(lib, "dbghelp.lib")
#endif

#define VOICETYPER_CRASH_DUMP_PREFIX    "voicetyper-crash-"
#define VOICETYPER_CRASH_DUMP_SUFFIX    ".dmp"
#define VOICETYPER_CRASH_SEEN_SUFFIX    ".seen"

struct DiagnosticsState
{
#ifdef _WIN32
	LPTOP_LEVEL_EXCEPTION_FILTER PreviousExceptionFilter;
#endif
};

static DiagnosticsState g_Diagnostics = {};

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

	MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), File,
		MiniDumpNormal, &Mei, nullptr, nullptr);

	CloseHandle(File);
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
}
