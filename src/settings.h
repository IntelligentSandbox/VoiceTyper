#pragma once

#include "settings_store.h"

#include <string>

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

inline
bool
save_int_setting(const char *Name, int Value)
{
	auto Map = read_settings_map();
	Map[Name] = std::to_string(Value);
	return write_settings_map(Map);
}

inline
bool
load_int_setting(const char *Name, int *OutValue)
{
	auto Map = read_settings_map();
	auto It = Map.find(Name);
	if (It == Map.end()) return false;

	size_t End = 0;
	try
	{
		*OutValue = std::stoi(It->second, &End);
	}
	catch (...)
	{
		return false;
	}

	if (End != It->second.size()) return false;
	return true;
}
