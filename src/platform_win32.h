#pragma once

#include <vector>
#include <string>

#include <windows.h>
#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <propkey.h>
#include <functiondiscoverykeys.h>
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "propsys.lib")

#include "qt.h"

#define MAX_AUDIO_DEVICE_NAME_LENGTH 512

struct AudioInputDeviceInfo
{
	int Index;
	std::string Id;
	std::string Name;
	bool IsDefault;
};

struct AudioCaptureDevice
{
	HWAVEIN Handle;
	int DeviceIndex;
	bool IsCapturing;
};

#define AUDIO_CAPTURE_SAMPLE_RATE       16000
#define AUDIO_CAPTURE_CHANNELS          1
#define AUDIO_CAPTURE_BITS_PER_SAMPLE   16
#define AUDIO_CAPTURE_BUFFER_MS         100
#define AUDIO_CAPTURE_BUFFER_COUNT      8

inline
std::vector<AudioInputDeviceInfo>
query_audio_input_devices_native()
{
	std::vector<AudioInputDeviceInfo> Devices;

	UINT NumDevices = waveInGetNumDevs();

	WAVEINCAPS2W Caps = {};
	MMRESULT Result;

	IMMDeviceEnumerator* pEnumerator = nullptr;
	IMMDeviceCollection* pCollection = nullptr;
	BOOL hasWASAPI = FALSE;

	CoInitializeEx(NULL, COINIT_MULTITHREADED);
	hasWASAPI = (CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator) == S_OK);

	if (hasWASAPI && pEnumerator)
	{
		pEnumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &pCollection);
	}

	if (hasWASAPI && pCollection)
	{
		UINT deviceCount = 0;
		pCollection->GetCount(&deviceCount);

		for (UINT i = 0; i < NumDevices && i < deviceCount; i++)
		{
			Result = waveInGetDevCapsW(i, (LPWAVEINCAPSW)&Caps, sizeof(Caps));
			if (Result != MMSYSERR_NOERROR) continue; // TODO(warren): Could have better error handling.

            AudioInputDeviceInfo Info;
            Info.Index = i;
            Info.Id = std::to_string(i);

            IMMDevice* pDevice = nullptr;
            IPropertyStore* pProps = nullptr;
            PROPVARIANT varName;

            PropVariantInit(&varName);

            BOOL gotName = FALSE;
            if (pCollection->Item(i, &pDevice) == S_OK)
            {
                if (pDevice->OpenPropertyStore(STGM_READ, &pProps) == S_OK)
                {
                    if (pProps->GetValue(PKEY_Device_FriendlyName, &varName) == S_OK)
                    {
                        if (varName.vt == VT_LPWSTR && varName.pwszVal)
                        {
                            char fullName[MAX_AUDIO_DEVICE_NAME_LENGTH];
                            WideCharToMultiByte(CP_UTF8, 0, varName.pwszVal, -1, fullName, MAX_AUDIO_DEVICE_NAME_LENGTH, NULL, NULL);
                            Info.Name = fullName;
                            gotName = TRUE;
                        }
                        PropVariantClear(&varName);
                    }
                    pProps->Release();
                }
                pDevice->Release();
            }

            if (!gotName)
            {
                char DeviceName[MAX_AUDIO_DEVICE_NAME_LENGTH];
                int converted = WideCharToMultiByte(CP_UTF8, 0, Caps.szPname, -1, DeviceName, MAX_AUDIO_DEVICE_NAME_LENGTH, NULL, NULL);
                if (converted == 0)
                {
                    DeviceName[0] = 0;
                }
                Info.Name = DeviceName;
            }

            Info.IsDefault = false;
            Devices.push_back(Info);
		}

		pCollection->Release();
	}
	else
	{
		for (UINT i = 0; i < NumDevices; i++)
		{
			Result = waveInGetDevCapsW(i, (LPWAVEINCAPSW)&Caps, sizeof(Caps));
			if (Result != MMSYSERR_NOERROR) continue; // TODO(warren): Could have better error handling.

            AudioInputDeviceInfo Info;
            Info.Index = i;
            Info.Id = std::to_string(i);
            char DeviceName[MAX_AUDIO_DEVICE_NAME_LENGTH];
            int converted = WideCharToMultiByte(CP_UTF8, 0, Caps.szPname, -1, DeviceName, MAX_AUDIO_DEVICE_NAME_LENGTH, NULL, NULL);
            if (converted == 0)
            {
                DeviceName[0] = 0;
            }
            Info.Name = DeviceName;
            Info.IsDefault = false;
            Devices.push_back(Info);
		}
	}

	if (pEnumerator)
	{
		pEnumerator->Release();
	}

	CoUninitialize();

	return Devices;
}

inline
void
inject_text_to_window(HWND TargetWindow, const char *Utf8Text)
{
	if (!TargetWindow || !Utf8Text || Utf8Text[0] == '\0') return;

	int WideLen = MultiByteToWideChar(CP_UTF8, 0, Utf8Text, -1, nullptr, 0);
	if (WideLen <= 1) return;

	std::wstring Wide(WideLen - 1, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, Utf8Text, -1, &Wide[0], WideLen);

	SetForegroundWindow(TargetWindow);
	Sleep(50);

	std::vector<INPUT> Inputs;
	Inputs.reserve(Wide.size() * 2);

	for (wchar_t Ch : Wide)
	{
		INPUT Down = {};
		Down.type           = INPUT_KEYBOARD;
		Down.ki.wScan       = Ch;
		Down.ki.dwFlags     = KEYEVENTF_UNICODE;
		Inputs.push_back(Down);

		INPUT Up = {};
		Up.type           = INPUT_KEYBOARD;
		Up.ki.wScan       = Ch;
		Up.ki.dwFlags     = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
		Inputs.push_back(Up);
	}

	SendInput((UINT)Inputs.size(), Inputs.data(), sizeof(INPUT));
}

inline
void
paste_text_to_window(HWND TargetWindow, const char *Utf8Text)
{
	if (!TargetWindow || !Utf8Text || Utf8Text[0] == '\0') return;

	int WideLen = MultiByteToWideChar(CP_UTF8, 0, Utf8Text, -1, nullptr, 0);
	if (WideLen <= 1) return;

	if (!OpenClipboard(nullptr)) return;
	EmptyClipboard();

	HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, WideLen * sizeof(wchar_t));
	if (!hMem)
	{
		CloseClipboard();
		return;
	}

	wchar_t *pMem = (wchar_t *)GlobalLock(hMem);
	MultiByteToWideChar(CP_UTF8, 0, Utf8Text, -1, pMem, WideLen);
	GlobalUnlock(hMem);
	SetClipboardData(CF_UNICODETEXT, hMem);
	CloseClipboard();

	SetForegroundWindow(TargetWindow);
	Sleep(50);

	INPUT Inputs[6] = {};

	Inputs[0].type = INPUT_KEYBOARD;
	Inputs[0].ki.wVk = VK_CONTROL;

	Inputs[1].type = INPUT_KEYBOARD;
	Inputs[1].ki.wVk = VK_SHIFT;

	Inputs[2].type = INPUT_KEYBOARD;
	Inputs[2].ki.wVk = 'V';

	Inputs[3].type = INPUT_KEYBOARD;
	Inputs[3].ki.wVk = 'V';
	Inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;

	Inputs[4].type = INPUT_KEYBOARD;
	Inputs[4].ki.wVk = VK_SHIFT;
	Inputs[4].ki.dwFlags = KEYEVENTF_KEYUP;

	Inputs[5].type = INPUT_KEYBOARD;
	Inputs[5].ki.wVk = VK_CONTROL;
	Inputs[5].ki.dwFlags = KEYEVENTF_KEYUP;

	SendInput(6, Inputs, sizeof(INPUT));
}

inline
int
query_logical_processor_count()
{
	SYSTEM_INFO SysInfo = {};
	GetSystemInfo(&SysInfo);
	return (int)SysInfo.dwNumberOfProcessors;
}

inline
const char*
audio_device_get_name(AudioInputDeviceInfo *Info)
{
	if (Info)
	{
		return Info->Name.c_str();
	}
	return "";
}

inline
int
audio_device_get_index(AudioInputDeviceInfo *Info)
{
	if (Info)
	{
		return Info->Index;
	}
	return -1;
}

// ---- Settings persistence (JSON via Qt) ----
// Data is stored in <exe_dir>/data/settings.json

inline
QString
get_settings_file_path()
{
	wchar_t ExePath[MAX_PATH] = {};
	GetModuleFileNameW(nullptr, ExePath, MAX_PATH);
	QString ExeDir = QFileInfo(QString::fromWCharArray(ExePath)).absolutePath();
	return ExeDir + "/data/settings.json";
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
			printf("[platform] ERROR: could not write settings file: %s\n",
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
			printf("[platform] Saved %s: modifiers=%d key=%d\n", JsonKey, Modifiers, Key);
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
		printf("[platform] Loaded %s: modifiers=%d key=%d\n", JsonKey, *OutModifiers, *OutKey);
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
			printf("[platform] Saved %s: %s\n", JsonKey, Value ? "true" : "false");
	#endif

	return Ok;
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
		printf("[platform] Loaded %s: %s\n", JsonKey, *OutValue ? "true" : "false");
	#endif

	return true;
}

inline
void
play_start_recording_sound()
{
    Beep(1000, 200);
}

inline
void
play_stop_recording_sound()
{
    Beep(800, 200);
}

inline
void
play_cancel_recording_sound()
{
    Beep(400, 300);
}
