#pragma once

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

// Reads the whole JSON root object from disk, or returns an empty object.
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

// Writes a root object back to disk, creating data/ if needed.
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

// Saves a single named hotkey (e.g. "record_hotkey") into the JSON file,
// preserving all other existing keys.
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

// Loads a single named hotkey from the JSON file into OutModifiers / OutKey.
// Returns false if the file or key is missing; caller should keep the default.
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

// Saves a boolean setting (e.g., "play_record_sound") to the JSON file.
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

// Saves a string setting to the JSON file.
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

// Loads a string setting from the JSON file.
// Returns false if the file or key is missing; caller should keep the default.
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

// Loads a boolean setting from the JSON file.
// Returns false if the file or key is missing; caller should keep the default.
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
