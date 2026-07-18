#pragma once

#include "host_services.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <string>

inline
std::string
get_settings_file_path()
{
	return platform_join_path(platform_get_exe_dir(), "settings.ini");
}

inline
void
cleanup_legacy_settings_json()
{
	std::string Path = platform_join_path(platform_get_exe_dir(), "settings.json");
	remove(Path.c_str());
}

inline
void
migrate_legacy_data_dir_settings()
{
	std::string NewPath = get_settings_file_path();
	std::string OldPath = platform_join_path(platform_get_exe_dir(), "data/settings.ini");

	FILE *OldFile = fopen(OldPath.c_str(), "r");
	if (!OldFile) return;
	fclose(OldFile);

	FILE *NewFile = fopen(NewPath.c_str(), "r");
	if (NewFile)
	{
		fseek(NewFile, 0, SEEK_END);
		long Size = ftell(NewFile);
		fclose(NewFile);
		if (Size > 0) return;
	}

	remove(NewPath.c_str());
	rename(OldPath.c_str(), NewPath.c_str());
}

inline
std::map<std::string, std::string>
read_settings_map()
{
	std::map<std::string, std::string> Map;
	FILE *F = fopen(get_settings_file_path().c_str(), "r");
	if (!F) return Map;

	char Line[512];
	while (fgets(Line, sizeof(Line), F))
	{
		char *Eq = strchr(Line, '=');
		if (!Eq) continue;
		*Eq = '\0';
		char *Val = Eq + 1;
		size_t Len = strlen(Val);
		while (Len > 0 && (Val[Len - 1] == '\n' || Val[Len - 1] == '\r'))
		{
			Val[--Len] = '\0';
		}
		Map[Line] = Val;
	}
	fclose(F);
	return Map;
}

inline
bool
write_settings_map(const std::map<std::string, std::string> &Map)
{
	std::string Path = get_settings_file_path();
	std::string Dir = Path.substr(0, Path.find_last_of("\\/"));
	if (!platform_ensure_directory(Dir))
		return false;

	FILE *F = fopen(Path.c_str(), "w");
	if (!F) return false;

	for (const auto &Pair : Map)
	{
		fprintf(F, "%s=%s\n", Pair.first.c_str(), Pair.second.c_str());
	}

	fclose(F);
	return true;
}
