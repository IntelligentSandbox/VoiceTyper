#pragma once

#include "build_time_constants.h"
#include "host_services.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

inline bool
diag_has_seen_sidecar(const std::string &DumpPath)
{
	std::string SeenPath = DumpPath + CRASH_SEEN_SUFFIX;
	FILE *F = fopen(SeenPath.c_str(), "r");
	if (!F) return false;
	fclose(F);
	return true;
}

inline void
check_for_previous_crash_dumps(std::vector<std::string> *OutPaths)
{
	OutPaths->clear();

	std::string Dir = platform_get_data_dir();
	std::vector<PlatformFileInfo> Files = platform_list_files(Dir);

	size_t SuffixLen = strlen(CRASH_DUMP_SUFFIX);

	for (const PlatformFileInfo &File : Files)
	{
		if (File.Name.rfind(CRASH_DUMP_PREFIX, 0) != 0) continue;

		size_t SuffixPos = File.Name.rfind(CRASH_DUMP_SUFFIX);
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
	std::string SeenPath = DumpPath + CRASH_SEEN_SUFFIX;
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
	platform_init_crash_diagnostics();
}

inline void
shutdown_diagnostics()
{
	platform_shutdown_crash_diagnostics();
}
