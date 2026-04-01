#pragma once

#ifdef VOICETYPER_USE_IMGUI

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

#else // Qt build

#include "qt.h"
#include "platform.h"

#include <string>

// ---- Settings persistence (JSON via Qt) ----
// Data is stored in <exe_dir>/data/settings.json

inline
QString
get_settings_file_path()
{
	std::string ExeDir = platform_get_exe_dir();
	return QString::fromStdString(ExeDir) + "/data/settings.json";
}

inline
QJsonObject
read_settings_root()
{
	QFile File(get_settings_file_path());
	if (!File.open(QIODevice::ReadOnly)) return QJsonObject();
	QJsonDocument Doc = QJsonDocument::fromJson(File.readAll());
	File.close();
	if (Doc.isNull() || !Doc.isObject()) return QJsonObject();
	return Doc.object();
}

inline
bool
write_settings_root(const QJsonObject &Root)
{
	QString FilePath = get_settings_file_path();
	QDir().mkpath(QFileInfo(FilePath).absolutePath());

	QFile File(FilePath);
	if (!File.open(QIODevice::WriteOnly | QIODevice::Truncate))
	{
		#ifdef DEBUG
			printf("[settings] ERROR: could not write settings file: %s\n",
				FilePath.toUtf8().constData());
		#endif
		return false;
	}
	File.write(QJsonDocument(Root).toJson(QJsonDocument::Indented));
	File.close();
	return true;
}

inline
bool
save_hotkey_setting(const char *JsonKey, int Modifiers, int Key)
{
	QJsonObject Root = read_settings_root();

	QJsonObject Obj;
	Obj["modifiers"] = Modifiers;
	Obj["key"]       = Key;
	Root[JsonKey]    = Obj;

	bool Ok = write_settings_root(Root);

	#ifdef DEBUG
		if (Ok)
			printf("[settings] Saved %s: modifiers=%d key=%d\n", JsonKey, Modifiers, Key);
	#endif

	return Ok;
}

inline
bool
load_hotkey_setting(const char *JsonKey, int *OutModifiers, int *OutKey)
{
	QJsonObject Root = read_settings_root();
	if (!Root.contains(JsonKey)) return false;

	QJsonObject Obj = Root[JsonKey].toObject();
	if (!Obj.contains("modifiers") || !Obj.contains("key")) return false;

	*OutModifiers = Obj["modifiers"].toInt();
	*OutKey       = Obj["key"].toInt();

	#ifdef DEBUG
		printf("[settings] Loaded %s: modifiers=%d key=%d\n", JsonKey, *OutModifiers, *OutKey);
	#endif

	return true;
}

inline
bool
save_bool_setting(const char *JsonKey, bool Value)
{
	QJsonObject Root = read_settings_root();
	Root[JsonKey] = Value;
	bool Ok = write_settings_root(Root);

	#ifdef DEBUG
		if (Ok)
			printf("[settings] Saved %s: %s\n", JsonKey, Value ? "true" : "false");
	#endif

	return Ok;
}

inline
bool
save_string_setting(const char *JsonKey, const char *Value)
{
	QJsonObject Root = read_settings_root();
	Root[JsonKey] = QString::fromUtf8(Value);
	bool Ok = write_settings_root(Root);

	#ifdef DEBUG
		if (Ok)
			printf("[settings] Saved %s: %s\n", JsonKey, Value);
	#endif

	return Ok;
}

inline
bool
load_string_setting(const char *JsonKey, std::string *OutValue)
{
	QJsonObject Root = read_settings_root();
	if (!Root.contains(JsonKey)) return false;

	*OutValue = Root[JsonKey].toString().toUtf8().constData();

	#ifdef DEBUG
		printf("[settings] Loaded %s: %s\n", JsonKey, OutValue->c_str());
	#endif

	return true;
}

inline
bool
load_bool_setting(const char *JsonKey, bool *OutValue)
{
	QJsonObject Root = read_settings_root();
	if (!Root.contains(JsonKey)) return false;

	*OutValue = Root[JsonKey].toBool();

	#ifdef DEBUG
		printf("[settings] Loaded %s: %s\n", JsonKey, *OutValue ? "true" : "false");
	#endif

	return true;
}

#endif
