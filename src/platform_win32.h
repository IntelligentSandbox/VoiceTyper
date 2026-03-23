#pragma once

#include "state.h"

#include <vector>
#include <string>
#include <cstring>

#include <windows.h>
#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <propkey.h>
#include <functiondiscoverykeys.h>
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "propsys.lib")

struct AudioCaptureDevice
{
	HWAVEIN Handle;
	int DeviceIndex;
	bool IsCapturing;
};

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
void
set_taskbar_icon(HWND Window, const char *PngPath)
{
	if (!Window || !PngPath) return;

	QPixmap Pixmap(PngPath);
	if (Pixmap.isNull())
	{
		#ifdef DEBUG
			printf("[platform] set_taskbar_icon: failed to load %s\n", PngPath);
		#endif
		return;
	}

	HICON BigIcon = Pixmap.scaled(
		GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON),
		Qt::KeepAspectRatio, Qt::SmoothTransformation).toImage().toHICON();
	HICON SmallIcon = Pixmap.scaled(
		GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
		Qt::KeepAspectRatio, Qt::SmoothTransformation).toImage().toHICON();

	if (BigIcon)
		SendMessageW(Window, WM_SETICON, ICON_BIG, (LPARAM)BigIcon);
	if (SmallIcon)
		SendMessageW(Window, WM_SETICON, ICON_SMALL, (LPARAM)SmallIcon);

	#ifdef DEBUG
		printf("[platform] set_taskbar_icon: big=%s small=%s\n",
			BigIcon ? "ok" : "failed", SmallIcon ? "ok" : "failed");
	#endif
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

inline
std::string
platform_get_exe_dir()
{
	wchar_t ExePath[MAX_PATH] = {};
	GetModuleFileNameW(nullptr, ExePath, MAX_PATH);
	QString ExeDir = QFileInfo(QString::fromWCharArray(ExePath)).absolutePath();
	return ExeDir.toStdString();
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
	std::vector<WaveInBuffer> Buffers;
	std::atomic<bool> *Running;
};

static void CALLBACK
wavein_proc(
	HWAVEIN   hWaveIn,
	UINT      uMsg,
	DWORD_PTR dwInstance,
	DWORD_PTR dwParam1,
	DWORD_PTR dwParam2)
{
	if (uMsg != WIM_DATA) return;

	AudioPipelineContext *Ctx = reinterpret_cast<AudioPipelineContext*>(dwInstance);
	SetEvent(Ctx->ReadyEvent);
}

// Win32 implementation of platform audio capture.
// Opens the WaveIn device, queues buffers, starts capture, and pumps completed
// buffers into AppState->AudioAccumBuffer until CaptureRunning goes false.
// Caller is responsible for setting CaptureRunning before calling.
static bool
run_platform_audio_capture(GlobalState *AppState, int DeviceIndex)
{
	const int SamplesPerBuffer = (AUDIO_CAPTURE_SAMPLE_RATE * AUDIO_CAPTURE_BUFFER_MS) / 1000;

	AudioPipelineContext PipeCtx = {};
	PipeCtx.Running = &AppState->CaptureRunning;

	PipeCtx.ReadyEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	if (!PipeCtx.ReadyEvent)
	{
		printf("[audio_pipeline] ERROR: Failed to create ready event\n");
		return false;
	}

	WAVEFORMATEX Format       = {};
	Format.wFormatTag         = WAVE_FORMAT_PCM;
	Format.nChannels          = AUDIO_CAPTURE_CHANNELS;
	Format.nSamplesPerSec     = AUDIO_CAPTURE_SAMPLE_RATE;
	Format.wBitsPerSample     = AUDIO_CAPTURE_BITS_PER_SAMPLE;
	Format.nBlockAlign        = (Format.nChannels * Format.wBitsPerSample) / 8;
	Format.nAvgBytesPerSec    = Format.nSamplesPerSec * Format.nBlockAlign;
	Format.cbSize             = 0;

	MMRESULT Res = waveInOpen(
		&PipeCtx.WaveInHandle,
		(UINT)DeviceIndex,
		&Format,
		(DWORD_PTR)wavein_proc,
		(DWORD_PTR)&PipeCtx,
		CALLBACK_FUNCTION);

	if (Res != MMSYSERR_NOERROR)
	{
		printf("[audio_pipeline] ERROR: waveInOpen failed (mmresult=%u)\n", Res);
		CloseHandle(PipeCtx.ReadyEvent);
		return false;
	}

	PipeCtx.Buffers.resize(AUDIO_CAPTURE_BUFFER_COUNT);
	for (int i = 0; i < AUDIO_CAPTURE_BUFFER_COUNT; i++)
	{
		WaveInBuffer &Buf         = PipeCtx.Buffers[i];
		Buf.Data.resize(SamplesPerBuffer);
		memset(&Buf.Header, 0, sizeof(WAVEHDR));
		Buf.Header.lpData         = reinterpret_cast<LPSTR>(Buf.Data.data());
		Buf.Header.dwBufferLength = (DWORD)(SamplesPerBuffer * sizeof(int16_t));

		waveInPrepareHeader(PipeCtx.WaveInHandle, &Buf.Header, sizeof(WAVEHDR));
		waveInAddBuffer(PipeCtx.WaveInHandle, &Buf.Header, sizeof(WAVEHDR));
	}

	waveInStart(PipeCtx.WaveInHandle);
	#ifdef DEBUG
		printf("[audio_pipeline] Capture started on device index %d\n", DeviceIndex);
	#endif

	while (AppState->CaptureRunning.load())
	{
		DWORD WaitResult = WaitForSingleObject(PipeCtx.ReadyEvent, 50);
		if (WaitResult == WAIT_TIMEOUT) continue;

		for (int i = 0; i < AUDIO_CAPTURE_BUFFER_COUNT; i++)
		{
			WAVEHDR &Hdr = PipeCtx.Buffers[i].Header;
			if (!(Hdr.dwFlags & WHDR_DONE)) continue;

			int SamplesGot = (int)(Hdr.dwBytesRecorded / sizeof(int16_t));
			if (SamplesGot > 0)
			{
				const int16_t *Src = PipeCtx.Buffers[i].Data.data();
				std::lock_guard<std::mutex> Lock(AppState->AudioBufferMutex);
				size_t OldSize = AppState->AudioAccumBuffer.size();
				AppState->AudioAccumBuffer.resize(OldSize + SamplesGot);
				for (int j = 0; j < SamplesGot; j++)
				{
					AppState->AudioAccumBuffer[OldSize + j] = Src[j] / 32768.0f;
				}
			}

			Hdr.dwFlags         = 0;
			Hdr.dwBytesRecorded = 0;
			waveInPrepareHeader(PipeCtx.WaveInHandle, &Hdr, sizeof(WAVEHDR));
			waveInAddBuffer(PipeCtx.WaveInHandle, &Hdr, sizeof(WAVEHDR));
		}
	}

	waveInStop(PipeCtx.WaveInHandle);
	waveInReset(PipeCtx.WaveInHandle);

	for (int i = 0; i < AUDIO_CAPTURE_BUFFER_COUNT; i++)
	{
		waveInUnprepareHeader(PipeCtx.WaveInHandle, &PipeCtx.Buffers[i].Header, sizeof(WAVEHDR));
	}

	waveInClose(PipeCtx.WaveInHandle);
	CloseHandle(PipeCtx.ReadyEvent);

	#ifdef DEBUG
		printf("[audio_pipeline] Capture stopped\n");
	#endif
	return true;
}
