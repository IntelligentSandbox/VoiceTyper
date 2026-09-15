#pragma once

#include "build_time_constants.h"
#include "settings_store.h"

#include <string>

inline bool
save_hotkey_setting(const char *Name, int Modifiers, int Key)
{
	auto Map = read_settings_map();
	Map[std::string(Name) + "_modifiers"] = std::to_string(Modifiers);
	Map[std::string(Name) + "_key"] = std::to_string(Key);
	return write_settings_map(Map);
}

inline bool
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

inline void
save_paste_hotkey_override_setting(const std::string &ProcessName, HotkeyConfig Hotkey)
{
	std::string Name = std::string(PASTE_HOTKEY_OVERRIDE_SETTING_PREFIX) + ProcessName;
	save_hotkey_setting(Name.c_str(), (int)Hotkey.Modifiers, (int)Hotkey.VirtualKey);
}

inline void
remove_paste_hotkey_override_setting(const std::string &ProcessName)
{
	auto Map = read_settings_map();
	Map.erase(std::string(PASTE_HOTKEY_OVERRIDE_SETTING_PREFIX) + ProcessName + "_modifiers");
	Map.erase(std::string(PASTE_HOTKEY_OVERRIDE_SETTING_PREFIX) + ProcessName + "_key");
	write_settings_map(Map);
}

inline bool
save_bool_setting(const char *Name, bool Value)
{
	auto Map = read_settings_map();
	Map[Name] = Value ? "1" : "0";
	return write_settings_map(Map);
}

inline bool
load_bool_setting(const char *Name, bool *OutValue)
{
	auto Map = read_settings_map();
	auto It = Map.find(Name);
	if (It == Map.end()) return false;
	*OutValue = (It->second == "1");
	return true;
}

inline bool
save_string_setting(const char *Name, const char *Value)
{
	auto Map = read_settings_map();
	Map[Name] = Value;
	return write_settings_map(Map);
}

inline bool
load_string_setting(const char *Name, std::string *OutValue)
{
	auto Map = read_settings_map();
	auto It = Map.find(Name);
	if (It == Map.end()) return false;
	*OutValue = It->second;
	return true;
}

inline bool
save_int_setting(const char *Name, int Value)
{
	auto Map = read_settings_map();
	Map[Name] = std::to_string(Value);
	return write_settings_map(Map);
}

inline bool
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

inline bool
save_window_size_setting(int Width, int Height)
{
	auto Map = read_settings_map();
	Map["window_width"] = std::to_string(Width);
	Map["window_height"] = std::to_string(Height);
	return write_settings_map(Map);
}

inline bool
load_window_size_setting(int *OutWidth, int *OutHeight)
{
	auto Map = read_settings_map();
	auto WidthIt = Map.find("window_width");
	auto HeightIt = Map.find("window_height");
	if (WidthIt == Map.end() || HeightIt == Map.end()) return false;

	size_t WidthEnd = 0;
	size_t HeightEnd = 0;
	try
	{
		*OutWidth = std::stoi(WidthIt->second, &WidthEnd);
		*OutHeight = std::stoi(HeightIt->second, &HeightEnd);
	}
	catch (...)
	{
		return false;
	}

	if (WidthEnd != WidthIt->second.size()) return false;
	if (HeightEnd != HeightIt->second.size()) return false;
	return true;
}

inline bool
save_window_position_setting(int X, int Y)
{
	auto Map = read_settings_map();
	Map["window_x"] = std::to_string(X);
	Map["window_y"] = std::to_string(Y);
	return write_settings_map(Map);
}

inline bool
load_window_position_setting(int *OutX, int *OutY)
{
	auto Map = read_settings_map();
	auto XIt = Map.find("window_x");
	auto YIt = Map.find("window_y");
	if (XIt == Map.end() || YIt == Map.end()) return false;

	size_t XEnd = 0;
	size_t YEnd = 0;
	try
	{
		*OutX = std::stoi(XIt->second, &XEnd);
		*OutY = std::stoi(YIt->second, &YEnd);
	}
	catch (...)
	{
		return false;
	}

	if (XEnd != XIt->second.size()) return false;
	if (YEnd != YIt->second.size()) return false;
	return true;
}
