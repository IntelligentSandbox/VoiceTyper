#pragma once

#include "host_services.h"
#include "state.h"
#include "perf.h"

#include <vector>
#include <string>
#include <cstring>
#include <ctime>
#include <thread>
#include <cmath>

#include <windows.h>
#include <dbghelp.h>
#include <dwmapi.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <mmdeviceapi.h>
#include <propkey.h>
#include <winhttp.h>
#include <functiondiscoverykeys.h>
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "propsys.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "winhttp.lib")

// ---------------------------------------------------------------------------
// Platform interface implementations (declared in platform.h)
// ---------------------------------------------------------------------------

inline std::vector<AudioInputDeviceInfo>
platform_query_audio_devices()
{
	std::vector<AudioInputDeviceInfo> Devices;

	UINT NumDevices = waveInGetNumDevs();

	HRESULT CoHr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	if (CoHr == RPC_E_CHANGED_MODE) CoHr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	bool ComOwned = (CoHr == S_OK);

	IMMDeviceEnumerator *pEnumerator = nullptr;
	IMMDeviceCollection *pCollection = nullptr;
	bool HasWASAPI = (CoCreateInstance(
		__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
		__uuidof(IMMDeviceEnumerator), (void **)&pEnumerator) == S_OK);

	LPWSTR DefaultEndpointId = nullptr;
	if (HasWASAPI && pEnumerator)
	{
		pEnumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &pCollection);

		IMMDevice *pDefault = nullptr;
		if (pEnumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &pDefault) == S_OK)
		{
			pDefault->GetId(&DefaultEndpointId);
			pDefault->Release();
		}
	}

	struct WasapiEndpoint { std::wstring Id; std::wstring Name; };
	std::vector<WasapiEndpoint> Endpoints;

	if (pCollection)
	{
		UINT Count = 0;
		pCollection->GetCount(&Count);

		for (UINT i = 0; i < Count; i++)
		{
			IMMDevice *pDevice = nullptr;
			if (pCollection->Item(i, &pDevice) != S_OK) continue;

			WasapiEndpoint Ep;
			LPWSTR DeviceId = nullptr;
			if (pDevice->GetId(&DeviceId) == S_OK)
			{
				Ep.Id = DeviceId;
				CoTaskMemFree(DeviceId);
			}

			IPropertyStore *pProps = nullptr;
			if (pDevice->OpenPropertyStore(STGM_READ, &pProps) == S_OK)
			{
				PROPVARIANT VarName;
				PropVariantInit(&VarName);
				if (pProps->GetValue(PKEY_Device_FriendlyName, &VarName) == S_OK)
				{
					if (VarName.vt == VT_LPWSTR && VarName.pwszVal) Ep.Name = VarName.pwszVal;
					PropVariantClear(&VarName);
				}
				pProps->Release();
			}

			pDevice->Release();
			Endpoints.push_back(Ep);
		}

		pCollection->Release();
	}

	for (UINT i = 0; i < NumDevices; i++)
	{
		WAVEINCAPS2W Caps = {};
		if (waveInGetDevCapsW(i, (LPWAVEINCAPSW)&Caps, sizeof(Caps)) != MMSYSERR_NOERROR) continue;

		AudioInputDeviceInfo Info;
		Info.Index = (int)i;
		Info.Id = std::to_string(i);
		Info.IsDefault = false;

		int WaveNameLen = (int)wcslen(Caps.szPname);
		bool GotName = false;

		for (auto &Ep : Endpoints)
		{
			if (Ep.Name.empty()) continue;
			if (wcsncmp(Ep.Name.c_str(), Caps.szPname, WaveNameLen) != 0) continue;

			char FullName[MAX_AUDIO_DEVICE_NAME_LENGTH];
			WideCharToMultiByte(CP_UTF8, 0, Ep.Name.c_str(), -1,
				FullName, MAX_AUDIO_DEVICE_NAME_LENGTH, NULL, NULL);
			Info.Name = FullName;
			GotName = true;

			if (DefaultEndpointId && Ep.Id == DefaultEndpointId) Info.IsDefault = true;
			break;
		}

		if (!GotName)
		{
			char DeviceName[MAX_AUDIO_DEVICE_NAME_LENGTH];
			int Converted = WideCharToMultiByte(CP_UTF8, 0, Caps.szPname, -1,
				DeviceName, MAX_AUDIO_DEVICE_NAME_LENGTH, NULL, NULL);
			if (Converted == 0) DeviceName[0] = 0;
			Info.Name = DeviceName;
		}

		Devices.push_back(Info);
	}

	if (DefaultEndpointId) CoTaskMemFree(DefaultEndpointId);
	if (pEnumerator) pEnumerator->Release();
	if (ComOwned) CoUninitialize();

	return Devices;
}

static void
platform_inject_text_char_by_char(HWND TargetWindow, const char *Utf8Text)
{
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

static bool
win32_open_clipboard_with_retry()
{
	for (int Attempt = 0; Attempt < 10; Attempt++)
	{
		if (OpenClipboard(nullptr)) return true;
		Sleep(10);
	}
	return false;
}

static bool
platform_set_clipboard_text_win32(const char *Utf8Text)
{
	int WideLen = MultiByteToWideChar(CP_UTF8, 0, Utf8Text, -1, nullptr, 0);
	if (WideLen <= 1) return false;

	if (!win32_open_clipboard_with_retry()) return false;
	EmptyClipboard();

	HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, WideLen * sizeof(wchar_t));
	if (!hMem)
	{
		CloseClipboard();
		return false;
	}

	wchar_t *pMem = (wchar_t *)GlobalLock(hMem);
	MultiByteToWideChar(CP_UTF8, 0, Utf8Text, -1, pMem, WideLen);
	GlobalUnlock(hMem);
	bool Ok = SetClipboardData(CF_UNICODETEXT, hMem) != nullptr;
	CloseClipboard();
	if (!Ok) GlobalFree(hMem);
	return Ok;
}

static bool
platform_get_clipboard_text_win32(std::wstring *Out, bool *HasText)
{
	if (!Out || !HasText || !win32_open_clipboard_with_retry()) return false;

	bool Ok = false;
	*HasText = false;
	HANDLE hData = GetClipboardData(CF_UNICODETEXT);
	if (hData)
	{
		const wchar_t *pMem = (const wchar_t *)GlobalLock(hData);
		if (pMem)
		{
			Out->assign(pMem);
			GlobalUnlock(hData);
			*HasText = !Out->empty();
		}
	}
	CloseClipboard();
	return true;
}

static bool
platform_restore_clipboard_text_win32(const std::wstring &Wide)
{
	if (Wide.empty()) return false;

	SIZE_T Bytes = (Wide.size() + 1) * sizeof(wchar_t);
	HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, Bytes);
	if (!hMem) return false;

	wchar_t *pMem = (wchar_t *)GlobalLock(hMem);
	if (!pMem)
	{
		GlobalFree(hMem);
		return false;
	}
	memcpy(pMem, Wide.c_str(), Bytes);
	GlobalUnlock(hMem);

	if (!win32_open_clipboard_with_retry())
	{
		GlobalFree(hMem);
		return false;
	}

	EmptyClipboard();
	bool Ok = SetClipboardData(CF_UNICODETEXT, hMem) != nullptr;
	CloseClipboard();
	if (!Ok) GlobalFree(hMem);
	return Ok;
}

static bool
platform_clear_clipboard_win32()
{
	if (!win32_open_clipboard_with_retry()) return false;
	EmptyClipboard();
	CloseClipboard();
	return true;
}

static void
platform_inject_text_via_paste(HWND TargetWindow, const char *Utf8Text, const HotkeyConfig &PasteHotkey,
	bool PreserveClipboard, int ClipboardRestoreDelayMs)
{
	int WideLen = MultiByteToWideChar(CP_UTF8, 0, Utf8Text, -1, nullptr, 0);
	if (WideLen <= 1) return;

	SetForegroundWindow(TargetWindow);
	Sleep(50);

	std::wstring PastedWide;
	std::wstring PreviousClipboard;
	bool HadPreviousText = false;
	bool InspectedClipboard = false;
	if (PreserveClipboard)
	{
		PastedWide.resize(WideLen - 1, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, Utf8Text, -1, &PastedWide[0], WideLen);
		InspectedClipboard = platform_get_clipboard_text_win32(&PreviousClipboard, &HadPreviousText);
	}

	if (!platform_set_clipboard_text_win32(Utf8Text)) return;

	WORD ModVk[4];
	int ModCount = 0;
	if (PasteHotkey.Modifiers & HOTKEY_MOD_CTRL)  ModVk[ModCount++] = VK_CONTROL;
	if (PasteHotkey.Modifiers & HOTKEY_MOD_ALT)   ModVk[ModCount++] = VK_MENU;
	if (PasteHotkey.Modifiers & HOTKEY_MOD_SHIFT) ModVk[ModCount++] = VK_SHIFT;
	if (PasteHotkey.Modifiers & HOTKEY_MOD_WIN)   ModVk[ModCount++] = VK_LWIN;

	bool HasKey = PasteHotkey.VirtualKey != APP_KEY_NONE;

	bool ModHeld[4] = {};
	for (int i = 0; i < ModCount; i++)
	{
		ModHeld[i] = (GetAsyncKeyState((int)ModVk[i]) & 0x8000) != 0;
	}

	INPUT Inputs[10] = {};
	int Count = 0;

	for (int i = 0; i < ModCount; i++)
	{
		if (ModHeld[i]) continue;
		Inputs[Count].type = INPUT_KEYBOARD;
		Inputs[Count].ki.wVk = ModVk[i];
		Count++;
	}

	if (HasKey)
	{
		Inputs[Count].type = INPUT_KEYBOARD;
		Inputs[Count].ki.wVk = (WORD)PasteHotkey.VirtualKey;
		Count++;

		Inputs[Count].type = INPUT_KEYBOARD;
		Inputs[Count].ki.wVk = (WORD)PasteHotkey.VirtualKey;
		Inputs[Count].ki.dwFlags = KEYEVENTF_KEYUP;
		Count++;
	}

	for (int i = ModCount - 1; i >= 0; i--)
	{
		if (ModHeld[i]) continue;
		Inputs[Count].type = INPUT_KEYBOARD;
		Inputs[Count].ki.wVk = ModVk[i];
		Inputs[Count].ki.dwFlags = KEYEVENTF_KEYUP;
		Count++;
	}

	SendInput(Count, Inputs, sizeof(INPUT));

	if (!PreserveClipboard) return;

	if (ClipboardRestoreDelayMs < 0) ClipboardRestoreDelayMs = 0;
	if (ClipboardRestoreDelayMs > 10000) ClipboardRestoreDelayMs = 10000;
	if (ClipboardRestoreDelayMs < 500) ClipboardRestoreDelayMs = 500;
	Sleep((DWORD)ClipboardRestoreDelayMs);

	std::wstring CurrentClipboard;
	bool CurrentHasText = false;
	if (!platform_get_clipboard_text_win32(&CurrentClipboard, &CurrentHasText)) return;
	if (!CurrentHasText || CurrentClipboard != PastedWide) return;
	if (!InspectedClipboard) return;

	if (HadPreviousText) platform_restore_clipboard_text_win32(PreviousClipboard);
	else platform_clear_clipboard_win32();
}

inline void
platform_inject_text(PlatformRuntimeState *Platform, void *Window, const char *Utf8, bool CharByChar,
	HotkeyConfig PasteHotkey, bool PreserveClipboard, int ClipboardRestoreDelayMs)
{
	(void)Platform;
	HWND HWnd = (HWND)Window;
	if (!HWnd || !Utf8 || Utf8[0] == '\0') return;

	if (CharByChar) platform_inject_text_char_by_char(HWnd, Utf8);
	else platform_inject_text_via_paste(HWnd, Utf8, PasteHotkey, PreserveClipboard, ClipboardRestoreDelayMs);
}

inline void
platform_set_clipboard_text(PlatformRuntimeState *Platform, const char *Utf8)
{
	(void)Platform;
	if (!Utf8 || Utf8[0] == '\0') return;
	platform_set_clipboard_text_win32(Utf8);
}

inline void *
platform_get_foreground_window(PlatformRuntimeState *Platform)
{
	(void)Platform;
	return (void*)GetForegroundWindow();
}

inline void
platform_set_taskbar_icon(void *Window, const char *PngPath)
{
	HWND HWnd = (HWND)Window;
	if (!HWnd || !PngPath) return;

	HICON Icon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101));
	if (Icon)
	{
		SendMessageW(HWnd, WM_SETICON, ICON_BIG, (LPARAM)Icon);
		SendMessageW(HWnd, WM_SETICON, ICON_SMALL, (LPARAM)Icon);
	}
}

inline void
platform_apply_window_theme(void *Window, bool LightMode)
{
	HWND HWnd = (HWND)Window;
	if (!HWnd) return;

	BOOL Dark = LightMode ? FALSE : TRUE;
	DwmSetWindowAttribute(HWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &Dark, sizeof(Dark));

	COLORREF CaptionColor = LightMode ? RGB(243, 243, 243) : RGB(26, 26, 26);
	DwmSetWindowAttribute(HWnd, DWMWA_CAPTION_COLOR, &CaptionColor, sizeof(CaptionColor));
}

inline void
platform_play_sound(PlatformRuntimeState *Platform, int FreqHz, int DurationMs)
{
	(void)Platform;
	if (DurationMs <= 0) return;
	if (FreqHz < 20) return;

	const int FixedVolume = SOUND_DEFAULT_VOLUME;
	if (FixedVolume <= 0) return;

	const int SampleRate = 44100;
	const int NumSamples = (SampleRate * DurationMs) / 1000;
	const int AttackMs = 8;
	const int AttackSamples = (SampleRate * AttackMs) / 1000;

	std::thread([FreqHz, DurationMs, FixedVolume, SampleRate, NumSamples, AttackSamples]() {
		WAVEFORMATEX Wfx = {};
		Wfx.wFormatTag = WAVE_FORMAT_PCM;
		Wfx.nChannels = 1;
		Wfx.nSamplesPerSec = SampleRate;
		Wfx.wBitsPerSample = 16;
		Wfx.nBlockAlign = Wfx.nChannels * Wfx.wBitsPerSample / 8;
		Wfx.nAvgBytesPerSec = Wfx.nSamplesPerSec * Wfx.nBlockAlign;

		HWAVEOUT HWaveOut = nullptr;
		if (waveOutOpen(&HWaveOut, WAVE_MAPPER, &Wfx, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) return;

		short *Buffer = new short[NumSamples];
		const float PeakAmplitude = (FixedVolume / 100.0f) * 32767.0f;
		const float Pi2 = 6.2831853f;
		const float Harmonic2Gain = 0.18f;
		const float Harmonic3Gain = 0.06f;
		const float Norm = 1.0f / (1.0f + Harmonic2Gain + Harmonic3Gain);
		const float TauSec = -(DurationMs / 1000.0f) / logf(0.05f);
		const float DecayPerSample = expf(-1.0f / (SampleRate * TauSec));

		float Amp = 1.0f;
		for (int i = 0; i < NumSamples; i++)
		{
			float t = (float)i / (float)SampleRate;
			float Sample = sinf(Pi2 * FreqHz * t)
			             + Harmonic2Gain * sinf(Pi2 * 2.0f * FreqHz * t)
			             + Harmonic3Gain * sinf(Pi2 * 3.0f * FreqHz * t);
			Sample *= Norm;

			Amp *= DecayPerSample;
			float Env = Amp;
			if (i < AttackSamples) Env *= (float)i / (float)AttackSamples;

			Sample *= PeakAmplitude * Env;
			if (Sample > 32767.0f) Sample = 32767.0f;
			if (Sample < -32768.0f) Sample = -32768.0f;
			Buffer[i] = (short)Sample;
		}

	WAVEHDR Hdr = {};
	Hdr.lpData = (LPSTR)Buffer;
	Hdr.dwBufferLength = NumSamples * sizeof(short);

	if (waveOutPrepareHeader(HWaveOut, &Hdr, sizeof(Hdr)) == MMSYSERR_NOERROR)
	{
		if (waveOutWrite(HWaveOut, &Hdr, sizeof(Hdr)) == MMSYSERR_NOERROR)
		{
			while (!(Hdr.dwFlags & WHDR_DONE))
				Sleep(1);
		}
		waveOutUnprepareHeader(HWaveOut, &Hdr, sizeof(Hdr));
	}
	waveOutClose(HWaveOut);
	delete[] Buffer;
	}).detach();
}

inline bool
platform_is_key_down(AppKeyCode Key)
{
	if (Key == APP_KEY_WIN)
		return (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0
		    || (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;
	return (GetAsyncKeyState((int)Key) & 0x8000) != 0;
}

inline std::string
platform_get_binary_path()
{
	char ExePath[MAX_PATH] = {};
	GetModuleFileNameA(nullptr, ExePath, MAX_PATH);
	return std::string(ExePath);
}

inline std::string
platform_get_binary_dir()
{
	std::string ExePath = platform_get_binary_path();
	size_t LastSlash = ExePath.find_last_of("\\/");
	if (LastSlash != std::string::npos) ExePath.resize(LastSlash);
	return ExePath;
}

inline std::string
platform_get_data_dir()
{
	return platform_get_binary_dir();
}

inline bool
platform_ensure_directory(const std::string &Path)
{
	if (Path.empty()) return false;

	if (CreateDirectoryA(Path.c_str(), nullptr)) return true;

	DWORD Error = GetLastError();
	return Error == ERROR_ALREADY_EXISTS;
}

inline bool
platform_remove_empty_directory(const std::string &Path)
{
	if (Path.empty()) return false;
	return RemoveDirectoryA(Path.c_str()) != 0;
}

inline std::vector<PlatformFileInfo>
platform_list_files(const std::string &Dir)
{
	std::vector<PlatformFileInfo> Files;
	WIN32_FIND_DATAA Fd;
	std::string Pattern = platform_join_path(Dir, "*");
	HANDLE Hf = FindFirstFileA(Pattern.c_str(), &Fd);

	if (Hf == INVALID_HANDLE_VALUE) return Files;

	do
	{
		if (Fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

		LARGE_INTEGER FileSize;
		FileSize.LowPart = Fd.nFileSizeLow;
		FileSize.HighPart = Fd.nFileSizeHigh;

		PlatformFileInfo Info = {};
		Info.Name = Fd.cFileName;
		Info.SizeBytes = FileSize.QuadPart;
		Files.push_back(Info);
	} while (FindNextFileA(Hf, &Fd));

	FindClose(Hf);
	return Files;
}

// ---------------------------------------------------------------------------
// Win32 audio capture internals
// ---------------------------------------------------------------------------

struct WaveInBuffer
{
	WAVEHDR Header;
	std::vector<int16_t> Data;
};

struct AudioPipelineContext
{
	HWAVEIN WaveInHandle;
	HANDLE  ReadyEvent;
	std::vector<WaveInBuffer> *Buffers;
	std::atomic<bool> *Running;
};

// The capture device is kept open between record takes ("pre-warmed"): the
// ~15-25ms waveInOpen cost is paid once per device (and after device changes),
// not once per take. The device is idle-but-open between takes (waveInStop'd),
// and closed on shutdown, device switch, or start failure.
struct Win32WarmCaptureDevice
{
	HWAVEIN                   WaveInHandle;
	HANDLE                    ReadyEvent;
	std::vector<WaveInBuffer> Buffers;
	int                       DeviceIndex;
};

static Win32WarmCaptureDevice g_WarmCapture = {};
static std::mutex             g_WarmCaptureMutex;

static void CALLBACK
wavein_proc(
	HWAVEIN   hWaveIn,
	UINT      uMsg,
	DWORD_PTR dwInstance,
	DWORD_PTR dwParam1,
	DWORD_PTR dwParam2)
{
	if (uMsg != WIM_DATA) return;

	Win32WarmCaptureDevice *Warm = reinterpret_cast<Win32WarmCaptureDevice*>(dwInstance);
	SetEvent(Warm->ReadyEvent);
}

static void
win32_close_warm_capture_locked()
{
	if (!g_WarmCapture.WaveInHandle) return;

	for (size_t i = 0; i < g_WarmCapture.Buffers.size(); i++)
	{
		waveInUnprepareHeader(g_WarmCapture.WaveInHandle, &g_WarmCapture.Buffers[i].Header, sizeof(WAVEHDR));
	}
	waveInClose(g_WarmCapture.WaveInHandle);
	CloseHandle(g_WarmCapture.ReadyEvent);
	g_WarmCapture = {};
}

static bool
win32_open_warm_capture_locked(int DeviceIndex)
{
	const int SamplesPerBuffer = (AUDIO_CAPTURE_SAMPLE_RATE * AUDIO_CAPTURE_BUFFER_MS) / 1000;

	g_WarmCapture.ReadyEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	if (!g_WarmCapture.ReadyEvent) return false;

	WAVEFORMATEX Format       = {};
	Format.wFormatTag         = WAVE_FORMAT_PCM;
	Format.nChannels          = AUDIO_CAPTURE_CHANNELS;
	Format.nSamplesPerSec     = AUDIO_CAPTURE_SAMPLE_RATE;
	Format.wBitsPerSample     = AUDIO_CAPTURE_BITS_PER_SAMPLE;
	Format.nBlockAlign        = (Format.nChannels * Format.wBitsPerSample) / 8;
	Format.nAvgBytesPerSec    = Format.nSamplesPerSec * Format.nBlockAlign;
	Format.cbSize             = 0;

	MMRESULT Res = waveInOpen(
		&g_WarmCapture.WaveInHandle,
		(UINT)DeviceIndex,
		&Format,
		(DWORD_PTR)wavein_proc,
		(DWORD_PTR)&g_WarmCapture,
		CALLBACK_FUNCTION);

	if (Res != MMSYSERR_NOERROR)
	{
		printf("[audio_pipeline] ERROR: waveInOpen failed (mmresult=%u)\n", Res);
		CloseHandle(g_WarmCapture.ReadyEvent);
		g_WarmCapture = {};
		return false;
	}

	g_WarmCapture.DeviceIndex = DeviceIndex;
	g_WarmCapture.Buffers.resize(AUDIO_CAPTURE_BUFFER_COUNT);
	for (int i = 0; i < AUDIO_CAPTURE_BUFFER_COUNT; i++)
	{
		WaveInBuffer &Buf         = g_WarmCapture.Buffers[i];
		Buf.Data.resize(SamplesPerBuffer);
		memset(&Buf.Header, 0, sizeof(WAVEHDR));
		Buf.Header.lpData         = reinterpret_cast<LPSTR>(Buf.Data.data());
		Buf.Header.dwBufferLength = (DWORD)(SamplesPerBuffer * sizeof(int16_t));

		waveInPrepareHeader(g_WarmCapture.WaveInHandle, &Buf.Header, sizeof(WAVEHDR));
	}

	return true;
}

static void
win32_harvest_wavein_buffers(
	AudioPipelineContext *PipeCtx,
	GlobalState *AppState,
	int64_t RequestNs,
	bool *GotFirstSamples,
	bool Requeue)
{
	constexpr int SamplesPerBuffer = (AUDIO_CAPTURE_SAMPLE_RATE * AUDIO_CAPTURE_BUFFER_MS) / 1000;

	for (int i = 0; i < AUDIO_CAPTURE_BUFFER_COUNT; i++)
	{
		WAVEHDR &Hdr = (*PipeCtx->Buffers)[i].Header;
		if (!(Hdr.dwFlags & WHDR_DONE)) continue;

		int SamplesGot = (int)(Hdr.dwBytesRecorded / sizeof(int16_t));
		if (SamplesGot > 0)
		{
			const int16_t *Src = (*PipeCtx->Buffers)[i].Data.data();
			float Converted[SamplesPerBuffer];
			for (int j = 0; j < SamplesGot; j++)
			{
				Converted[j] = Src[j] / 32768.0f;
			}

			{
				std::lock_guard<std::mutex> Lock(AppState->AudioBufferMutex);
				clip_append(&AppState->AudioPool, &AppState->AudioAccum, Converted, SamplesGot);
			}

			if (!*GotFirstSamples)
			{
				*GotFirstSamples = true;
				AppState->LastRecordFirstAudioMs.store((double)(perf_now_ns() - RequestNs) / 1000000.0);
				perf_event("audio_first_samples");
			}
		}

		if (!Requeue) continue;

		Hdr.dwFlags         = 0;
		Hdr.dwBytesRecorded = 0;
		waveInPrepareHeader(PipeCtx->WaveInHandle, &Hdr, sizeof(WAVEHDR));
		waveInAddBuffer(PipeCtx->WaveInHandle, &Hdr, sizeof(WAVEHDR));
	}
}

static void
win32_queue_all_capture_buffers(AudioPipelineContext *PipeCtx)
{
	for (int i = 0; i < AUDIO_CAPTURE_BUFFER_COUNT; i++)
	{
		WAVEHDR &Hdr         = (*PipeCtx->Buffers)[i].Header;
		Hdr.dwFlags         = 0;
		Hdr.dwBytesRecorded = 0;
		waveInPrepareHeader(PipeCtx->WaveInHandle, &Hdr, sizeof(WAVEHDR));
		waveInAddBuffer(PipeCtx->WaveInHandle, &Hdr, sizeof(WAVEHDR));
	}
}

inline bool
platform_audio_capture(PlatformRuntimeState *Platform, GlobalState *AppState, int DeviceIndex)
{
	(void)Platform;

	const int64_t RequestNs = AppState->PipelineRequestNs.load();
	bool GotFirstSamples = false;

	AudioPipelineContext PipeCtx = {};
	PipeCtx.Running = &AppState->CaptureRunning;

	std::lock_guard<std::mutex> WarmLock(g_WarmCaptureMutex);

	if (g_WarmCapture.WaveInHandle && g_WarmCapture.DeviceIndex != DeviceIndex)
	{
		win32_close_warm_capture_locked();
	}

	if (!g_WarmCapture.WaveInHandle && !win32_open_warm_capture_locked(DeviceIndex))
	{
		return false;
	}

	PipeCtx.WaveInHandle = g_WarmCapture.WaveInHandle;
	PipeCtx.ReadyEvent   = g_WarmCapture.ReadyEvent;
	PipeCtx.Buffers      = &g_WarmCapture.Buffers;

	AppState->LastRecordDeviceOpenMs.store((double)(perf_now_ns() - RequestNs) / 1000000.0);
	perf_event("audio_device_open");

	win32_queue_all_capture_buffers(&PipeCtx);

	MMRESULT StartRes = waveInStart(PipeCtx.WaveInHandle);
	if (StartRes != MMSYSERR_NOERROR)
	{
		// The warm device died underneath us (unplug, driver reset). Drop it and
		// retry once with a cold open.
		printf("[audio_pipeline] waveInStart failed on warm device (mmresult=%u); reopening\n", StartRes);
		win32_close_warm_capture_locked();
		if (!win32_open_warm_capture_locked(DeviceIndex)) return false;

		PipeCtx.WaveInHandle = g_WarmCapture.WaveInHandle;
		PipeCtx.ReadyEvent   = g_WarmCapture.ReadyEvent;
		PipeCtx.Buffers      = &g_WarmCapture.Buffers;

		win32_queue_all_capture_buffers(&PipeCtx);
		if (waveInStart(PipeCtx.WaveInHandle) != MMSYSERR_NOERROR)
		{
			win32_close_warm_capture_locked();
			return false;
		}
	}

	AppState->LastRecordCaptureStartMs.store((double)(perf_now_ns() - RequestNs) / 1000000.0);
	perf_event("audio_capture_started");

	while (AppState->CaptureRunning.load())
	{
		DWORD WaitResult = WaitForSingleObject(PipeCtx.ReadyEvent, 50);
		if (WaitResult == WAIT_TIMEOUT) continue;

		win32_harvest_wavein_buffers(&PipeCtx, AppState, RequestNs, &GotFirstSamples, true);
	}

	PerfSpan DeviceCloseSpan("audio_device_close");

	waveInStop(PipeCtx.WaveInHandle);
	waveInReset(PipeCtx.WaveInHandle);

	// waveInReset returns every still-queued buffer as WHDR_DONE with whatever
	// partial samples it holds — harvest them or the tail of the utterance
	// (up to one buffer period) is silently dropped.
	win32_harvest_wavein_buffers(&PipeCtx, AppState, RequestNs, &GotFirstSamples, false);

	// Device stays open (pre-warmed) for the next take; buffers are all DONE
	// now and get re-queued by the next session start.

	return true;
}

inline void
platform_close_warm_audio_device()
{
	std::lock_guard<std::mutex> Lock(g_WarmCaptureMutex);
	win32_close_warm_capture_locked();
}

inline bool
platform_spawn_detached(const std::string &CommandLine, const std::string &WorkingDir, bool Hidden)
{
	int WsLength = MultiByteToWideChar(CP_UTF8, 0, CommandLine.c_str(), -1, nullptr, 0);
	int WdLength = MultiByteToWideChar(CP_UTF8, 0, WorkingDir.c_str(), -1, nullptr, 0);
	if (WsLength <= 0 || WdLength <= 0)
	{
		return false;
	}

	std::wstring WideCommandLine((size_t)WsLength, L'\0');
	std::wstring WideWorkingDir((size_t)WdLength, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, CommandLine.c_str(), -1, WideCommandLine.data(), WsLength);
	MultiByteToWideChar(CP_UTF8, 0, WorkingDir.c_str(), -1, WideWorkingDir.data(), WdLength);

	STARTUPINFOW Si = {};
	Si.cb = sizeof(Si);
	PROCESS_INFORMATION Pi = {};
	DWORD Flags = Hidden ? (DETACHED_PROCESS | CREATE_NO_WINDOW) : 0;

	if (!CreateProcessW(nullptr, WideCommandLine.data(), nullptr, nullptr, FALSE,
		Flags, nullptr, WorkingDir.empty() ? nullptr : WideWorkingDir.c_str(), &Si, &Pi))
	{
		return false;
	}

	CloseHandle(Pi.hThread);
	CloseHandle(Pi.hProcess);
	return true;
}

inline bool
platform_is_installed_build()
{
	DWORD Value = 0;
	DWORD Size = sizeof(Value);
	LONG Result = RegGetValueW(HKEY_CURRENT_USER, L"Software\\VoiceTyper", L"installed",
		RRF_RT_REG_DWORD, nullptr, &Value, &Size);
	return Result == ERROR_SUCCESS && Value == 1;
}

inline int
platform_get_process_id()
{
	return (int)GetCurrentProcessId();
}

inline std::string
platform_get_temp_dir()
{
	WCHAR Buffer[MAX_PATH + 1] = {};
	DWORD Length = GetTempPathW(MAX_PATH + 1, Buffer);
	if (Length == 0 || Length > MAX_PATH)
	{
		return ".";
	}

	int Utf8Length = WideCharToMultiByte(CP_UTF8, 0, Buffer, (int)Length, nullptr, 0, nullptr, nullptr);
	if (Utf8Length <= 0)
	{
		return ".";
	}

	std::string Result((size_t)Utf8Length, '\0');
	WideCharToMultiByte(CP_UTF8, 0, Buffer, (int)Length, Result.data(), Utf8Length, nullptr, nullptr);
	while (!Result.empty() && (Result.back() == '\\' || Result.back() == '/'))
	{
		Result.pop_back();
	}
	return Result;
}

inline void
platform_open_url(const char *Url)
{
	int Length = MultiByteToWideChar(CP_UTF8, 0, Url, -1, nullptr, 0);
	if (Length <= 0)
	{
		return;
	}

	std::wstring WideUrl((size_t)Length, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, Url, -1, WideUrl.data(), Length);

	ShellExecuteW(nullptr, L"open", WideUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

static std::string
win32_wide_to_utf8(const std::wstring &Wide)
{
	int Length = WideCharToMultiByte(CP_UTF8, 0, Wide.c_str(), (int)Wide.size(), nullptr, 0, nullptr, nullptr);
	if (Length <= 0)
	{
		return std::string();
	}

	std::string Result((size_t)Length, '\0');
	WideCharToMultiByte(CP_UTF8, 0, Wide.c_str(), (int)Wide.size(), Result.data(), Length, nullptr, nullptr);
	return Result;
}

inline std::string
platform_get_window_process_name(void *Window)
{
	HWND HWnd = (HWND)Window;
	if (!HWnd) return "";

	DWORD Pid = 0;
	if (GetWindowThreadProcessId(HWnd, &Pid) == 0 || Pid == 0) return "";

	HANDLE Process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, Pid);
	if (!Process) return "";

	std::string Name;
	WCHAR PathW[MAX_PATH] = {};
	DWORD PathLen = MAX_PATH;
	if (QueryFullProcessImageNameW(Process, 0, PathW, &PathLen) && PathLen > 0)
	{
		std::string Path = win32_wide_to_utf8(std::wstring(PathW, PathLen));
		size_t Slash = Path.find_last_of("\\/");
		Name = (Slash == std::string::npos) ? Path : Path.substr(Slash + 1);
	}

	CloseHandle(Process);
	return Name;
}

inline std::vector<PlatformFontInfo>
platform_enumerate_fonts()
{
	std::vector<PlatformFontInfo> Fonts;

	HKEY Key = nullptr;
	if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts",
		0, KEY_READ, &Key) != ERROR_SUCCESS)
	{
		return Fonts;
	}

	std::string FontsDir = platform_path_from_universal("C:/Windows/Fonts/");

	for (DWORD Index = 0;; Index++)
	{
		WCHAR NameBuf[512] = {};
		DWORD NameLen = 512;
		BYTE ValueBuf[MAX_PATH * 2] = {};
		DWORD ValueLen = sizeof(ValueBuf);
		DWORD Type = 0;

		LONG Result = RegEnumValueW(Key, Index, NameBuf, &NameLen, nullptr, &Type, ValueBuf, &ValueLen);
		if (Result == ERROR_NO_MORE_ITEMS) break;
		if (Result != ERROR_SUCCESS) continue;
		if (Type != REG_SZ) continue;

		std::string Name = win32_wide_to_utf8(std::wstring(NameBuf, NameLen));
		std::wstring WideValue((wchar_t *)ValueBuf, ValueLen / sizeof(wchar_t));
		while (!WideValue.empty() && WideValue.back() == L'\0') WideValue.pop_back();
		std::string File = win32_wide_to_utf8(WideValue);

		size_t NameLen8 = Name.size();
		if (NameLen8 > 11 && Name.compare(NameLen8 - 11, 11, " (TrueType)") == 0) Name.resize(NameLen8 - 11);
		NameLen8 = Name.size();
		if (NameLen8 > 11 && Name.compare(NameLen8 - 11, 11, " (OpenType)") == 0) Name.resize(NameLen8 - 11);

		if (File.size() < 4) continue;
		std::string Extension = File.substr(File.size() - 4);
		for (char &Ch : Extension)
		{
			if (Ch >= 'A' && Ch <= 'Z') Ch = (char)(Ch - 'A' + 'a');
		}
		if (Extension != ".ttf" && Extension != ".otf") continue;

		PlatformFontInfo Info;
		Info.Name = Name;
		if (File.size() > 2 && (File[1] == ':' || File[0] == '\\'))
		{
			Info.Path = File;
		}
		else
		{
			Info.Path = FontsDir + File;
		}
		Fonts.push_back(Info);
	}

	RegCloseKey(Key);
	return Fonts;
}

inline std::string
platform_path_from_universal(const std::string &Path)
{
	std::string Result = Path;
	for (char &Ch : Result)
	{
		if (Ch == '/') Ch = '\\';
	}
	return Result;
}

inline std::string
platform_ggml_backend_library_path(const std::string &SearchDir, const char *BackendName)
{
	return platform_join_path(SearchDir, std::string("ggml-") + BackendName + ".dll");
}

// ---------------------------------------------------------------------------
// Crash dump
// ---------------------------------------------------------------------------

static LPTOP_LEVEL_EXCEPTION_FILTER g_Win32PreviousExceptionFilter = nullptr;

static std::string
win32_crash_dump_path_for_now()
{
	time_t Now = time(nullptr);
	tm LocalTm = {};
	localtime_s(&LocalTm, &Now);

	char TimeBuf[32];
	strftime(TimeBuf, sizeof(TimeBuf), "%Y%m%d-%H%M%S", &LocalTm);

	std::string Filename = CRASH_DUMP_PREFIX;
	Filename += TimeBuf;
	Filename += CRASH_DUMP_SUFFIX;

	return platform_join_path(platform_get_data_dir(), Filename);
}

static void
win32_write_minidump(EXCEPTION_POINTERS *ExceptionInfo)
{
	std::string DumpPath = win32_crash_dump_path_for_now();

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
win32_unhandled_exception_filter(EXCEPTION_POINTERS *ExceptionInfo)
{
	win32_write_minidump(ExceptionInfo);

	if (g_Win32PreviousExceptionFilter)
		return g_Win32PreviousExceptionFilter(ExceptionInfo);

	return EXCEPTION_EXECUTE_HANDLER;
}

inline void
platform_init_crash_diagnostics()
{
	g_Win32PreviousExceptionFilter = SetUnhandledExceptionFilter(win32_unhandled_exception_filter);
}

inline void
platform_shutdown_crash_diagnostics()
{
	SetUnhandledExceptionFilter(g_Win32PreviousExceptionFilter);
	g_Win32PreviousExceptionFilter = nullptr;
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

// ---------------------------------------------------------------------------
// Downloads / updater
// ---------------------------------------------------------------------------

static void
win32_http_close(HINTERNET Request, HINTERNET Connect, HINTERNET Session)
{
	if (Request) WinHttpCloseHandle(Request);
	if (Connect) WinHttpCloseHandle(Connect);
	if (Session) WinHttpCloseHandle(Session);
}

static bool
win32_http_get(const std::string &Url, FILE *File, std::string *OutBody,
	std::atomic<int64_t> *Downloaded, std::atomic<int64_t> *Total, std::atomic<bool> *Cancel)
{
	std::wstring WideUrl(Url.begin(), Url.end());
	URL_COMPONENTSW Comp = {};
	Comp.dwStructSize = sizeof(Comp);
	wchar_t HostBuf[256] = {};
	wchar_t PathBuf[2048] = {};
	Comp.lpszHostName = HostBuf;
	Comp.dwHostNameLength = sizeof(HostBuf) / sizeof(wchar_t);
	Comp.lpszUrlPath = PathBuf;
	Comp.dwUrlPathLength = sizeof(PathBuf) / sizeof(wchar_t);

	if (!WinHttpCrackUrl(WideUrl.c_str(), (DWORD)WideUrl.size(), 0, &Comp))
	{
		return false;
	}

	HINTERNET Session = WinHttpOpen(L"VoiceTyper",
		WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!Session) return false;

	WinHttpSetTimeouts(Session, 30000, 30000, 30000, 5000);

	INTERNET_PORT Port = Comp.nPort ? Comp.nPort : INTERNET_DEFAULT_HTTPS_PORT;
	HINTERNET Connect = WinHttpConnect(Session, Comp.lpszHostName, Port, 0);
	if (!Connect)
	{
		win32_http_close(nullptr, nullptr, Session);
		return false;
	}

	HINTERNET Request = WinHttpOpenRequest(Connect, L"GET", Comp.lpszUrlPath,
		nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
	if (!Request)
	{
		win32_http_close(nullptr, Connect, Session);
		return false;
	}

	if (!WinHttpSendRequest(Request,
		WINHTTP_NO_ADDITIONAL_HEADERS, 0,
		WINHTTP_NO_REQUEST_DATA, 0,
		WINHTTP_IGNORE_REQUEST_TOTAL_LENGTH, 0) ||
		!WinHttpReceiveResponse(Request, nullptr))
	{
		win32_http_close(Request, Connect, Session);
		return false;
	}

	DWORD StatusCode = 0;
	DWORD StatusCodeSize = sizeof(StatusCode);
	if (!WinHttpQueryHeaders(Request,
		WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		WINHTTP_HEADER_NAME_BY_INDEX, &StatusCode, &StatusCodeSize, WINHTTP_NO_HEADER_INDEX) ||
		StatusCode < 200 || StatusCode >= 300)
	{
		win32_http_close(Request, Connect, Session);
		return false;
	}

	if (Total)
	{
		DWORD ContentLength = 0;
		DWORD ContentLengthSize = sizeof(ContentLength);
		if (WinHttpQueryHeaders(Request,
			WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
			WINHTTP_HEADER_NAME_BY_INDEX, &ContentLength, &ContentLengthSize, WINHTTP_NO_HEADER_INDEX) &&
			ContentLength > 0)
		{
			Total->store((int64_t)ContentLength);
		}
	}

	const DWORD BufSize = 64 * 1024;
	std::vector<char> Buffer(BufSize);
	int64_t TotalRead = 0;

	for (;;)
	{
		if (Cancel && Cancel->load())
		{
			win32_http_close(Request, Connect, Session);
			return false;
		}

		DWORD BytesRead = 0;
		if (!WinHttpReadData(Request, Buffer.data(), BufSize, &BytesRead))
		{
			win32_http_close(Request, Connect, Session);
			return false;
		}
		if (BytesRead == 0) break;

		if (File)
		{
			if (fwrite(Buffer.data(), 1, BytesRead, File) != BytesRead)
			{
				win32_http_close(Request, Connect, Session);
				return false;
			}
		}
		else if (OutBody)
		{
			OutBody->append(Buffer.data(), BytesRead);
		}

		TotalRead += BytesRead;
		if (Downloaded) Downloaded->store(TotalRead);
	}

	win32_http_close(Request, Connect, Session);
	return TotalRead > 0;
}

inline bool
platform_http_get_string(const std::string &Url, std::string *OutBody)
{
	return win32_http_get(Url, nullptr, OutBody, nullptr, nullptr, nullptr);
}

inline void
platform_download_file_thread(GlobalState *AppState, std::string Url, std::string DestPath, int64_t ExpectedSize)
{
	AppState->Ui.Download.DownloadedBytes.store(0);
	AppState->Ui.Download.TotalBytes.store(ExpectedSize);

	std::string PartPath = DestPath + ".part";

	// TODO(warren): These are so ugly, what is this, an anonymous fn?
	auto Fail = [&]()
	{
		AppState->Ui.Download.Failed.store(true);
		AppState->Ui.Download.IsRunning.store(false);
	};

	// TODO(warren): Bad bad style, we should never have { } for one line if statements, should just be clean
	// `if (condition) statement;` Seems like even if this is mentioned in AGENTS.md, LLMs will still inevitably
	// forget when given a lot of stuff in context. Plenty of places in this file where the if statement is not
	// written in the right style.
	FILE *File = nullptr;
	fopen_s(&File, PartPath.c_str(), "wb");
	if (!File)
	{
		Fail();
		return;
	}

	bool Ok = win32_http_get(Url, File, nullptr, &AppState->Ui.Download.DownloadedBytes,
		&AppState->Ui.Download.TotalBytes, &AppState->Ui.Download.CancelRequested);

	fclose(File);

	if (!Ok)
	{
		remove(PartPath.c_str());
		Fail();
		return;
	}

	remove(DestPath.c_str());
	if (rename(PartPath.c_str(), DestPath.c_str()) != 0)
	{
		remove(PartPath.c_str());
		AppState->Ui.Download.Failed.store(true);
	}
	else
	{
		AppState->Ui.Download.Succeeded.store(true);
	}

	AppState->Ui.Download.IsRunning.store(false);
}

inline void
platform_cancel_model_download(GlobalState *AppState)
{
	(void)AppState;
}

inline void
platform_update_download_thread(GlobalState *AppState, std::string Url, std::string DestPath)
{
	UpdateState *U = &AppState->Ui.Update;

	FILE *File = nullptr;
	fopen_s(&File, DestPath.c_str(), "wb");
	if (!File)
	{
		U->DownloadFailed.store(true);
		U->DownloadRunning.store(false);
		return;
	}

	bool Ok = win32_http_get(Url, File, nullptr,
		&U->DownloadedBytes, &U->TotalBytes, &U->DownloadCancelRequested);

	fclose(File);

	if (Ok)
	{
		U->DownloadSucceeded.store(true);
	}
	else
	{
		remove(DestPath.c_str());
		U->DownloadFailed.store(true);
	}

	U->DownloadRunning.store(false);
}

inline void
platform_cancel_update_download(GlobalState *AppState)
{
	(void)AppState;
}

static bool
win32_write_file_bytes(const std::string &Path, const std::string &Content)
{
	FILE *File = nullptr;
	fopen_s(&File, Path.c_str(), "wb");
	if (!File)
	{
		return false;
	}

	bool Ok = fwrite(Content.data(), 1, Content.size(), File) == Content.size();
	fclose(File);
	return Ok;
}

static std::string
win32_forward_slashes(std::string Path)
{
	for (char &Ch : Path)
	{
		if (Ch == '\\') Ch = '/';
	}
	return Path;
}

static bool
win32_string_ends_with(const std::string &Text, const char *Suffix)
{
	size_t SuffixLength = strlen(Suffix);
	if (Text.size() < SuffixLength) return false;
	for (size_t i = 0; i < SuffixLength; i++)
	{
		char Ch = Text[Text.size() - SuffixLength + i];
		if (Ch >= 'A' && Ch <= 'Z') Ch = (char)(Ch - 'A' + 'a');
		if (Ch != Suffix[i]) return false;
	}
	return true;
}

static bool
win32_apply_portable_zip(const std::string &ZipPath)
{
	std::string ExeDir = win32_forward_slashes(platform_get_data_dir());
	std::string BatPath = win32_forward_slashes(
		platform_join_path(platform_get_temp_dir(), "voicetyper-apply-update.bat"));
	std::string Zip = win32_forward_slashes(ZipPath);

	char PidStr[32];
	sprintf_s(PidStr, sizeof(PidStr), "%d", platform_get_process_id());

	std::string Bat;
	Bat += "@echo off\r\n";
	Bat += ":waitloop\r\n";
	Bat += std::string("tasklist /fi \"PID eq ") + PidStr + "\" 2>nul | find /i \"" + PidStr + "\" >nul\r\n";
	Bat += "if not errorlevel 1 (\r\n";
	Bat += "ping -n 2 127.0.0.1 >nul\r\n";
	Bat += "goto waitloop\r\n";
	Bat += ")\r\n";
	Bat += "tar -xf \"" + Zip + "\" -C \"" + ExeDir + "\"\r\n";
	Bat += "if errorlevel 1 exit /b 1\r\n";
	Bat += "del \"" + Zip + "\"\r\n";
	Bat += "start \"\" \"" + ExeDir + "/VoiceTyper.exe\"\r\n";
	Bat += "del \"%~f0\"\r\n";

	if (!win32_write_file_bytes(BatPath, Bat))
	{
		return false;
	}

	std::string Cmd = "cmd.exe /c \"\"" + BatPath + "\"\"";
	return platform_spawn_detached(Cmd, platform_get_temp_dir(), true);
}

static bool
win32_apply_msi(const std::string &MsiPath)
{
	std::string Cmd = "msiexec /i \"" + MsiPath + "\"";
	return platform_spawn_detached(Cmd, "", false);
}

inline bool
platform_apply_update_package(const std::string &PackagePath)
{
	if (win32_string_ends_with(PackagePath, ".msi"))
	{
		return win32_apply_msi(PackagePath);
	}
	return win32_apply_portable_zip(PackagePath);
}

inline const char *
platform_update_asset_tag()
{
	return "-x64_win-";
}

inline bool
platform_asset_is_installer(const std::string &AssetName)
{
	return win32_string_ends_with(AssetName, ".msi");
}
