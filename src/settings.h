#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <map>

#include "platform.h"

inline
std::string
get_settings_file_path()
{
	return platform_get_exe_dir() + "\\data\\settings.ini";
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
	CreateDirectoryA(Dir.c_str(), nullptr);

	FILE *F = fopen(Path.c_str(), "w");
	if (!F) return false;

	for (const auto &Pair : Map)
	{
		fprintf(F, "%s=%s\n", Pair.first.c_str(), Pair.second.c_str());
	}

	fclose(F);
	return true;
}

inline
bool
save_hotkey_setting(const char *Name, int Modifiers, int Key)
{
	auto Map = read_settings_map();
	Map[std::string(Name) + "_modifiers"] = std::to_string(Modifiers);
	Map[std::string(Name) + "_key"] = std::to_string(Key);
	return write_settings_map(Map);
}

inline
bool
load_hotkey_setting(const char *Name, int *OutModifiers, int *OutKey)
{
	auto Map = read_settings_map();
	std::string ModKey = std::string(Name) + "_modifiers";
	std::string KeyKey = std::string(Name) + "_key";
	auto ModIt = Map.find(ModKey);
	auto KeyIt = Map.find(KeyKey);
	if (ModIt == Map.end() || KeyIt == Map.end()) return false;
	*OutModifiers = std::stoi(ModIt->second);
	*OutKey = std::stoi(KeyIt->second);
	return true;
}

inline
bool
save_bool_setting(const char *Name, bool Value)
{
	auto Map = read_settings_map();
	Map[Name] = Value ? "1" : "0";
	return write_settings_map(Map);
}

inline
bool
load_bool_setting(const char *Name, bool *OutValue)
{
	auto Map = read_settings_map();
	auto It = Map.find(Name);
	if (It == Map.end()) return false;
	*OutValue = (It->second == "1");
	return true;
}

inline
bool
save_string_setting(const char *Name, const char *Value)
{
	auto Map = read_settings_map();
	Map[Name] = Value;
	return write_settings_map(Map);
}

inline
bool
load_string_setting(const char *Name, std::string *OutValue)
{
	auto Map = read_settings_map();
	auto It = Map.find(Name);
	if (It == Map.end()) return false;
	*OutValue = It->second;
	return true;
}

