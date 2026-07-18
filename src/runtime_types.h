#pragma once

#include <cstdint>
#include <string>

using AppHotkeyModifiers = uint32_t;
using AppKeyCode = uint32_t;
using PlatformWindowHandle = void*;

constexpr AppHotkeyModifiers HOTKEY_MOD_CTRL  = 0x01;
constexpr AppHotkeyModifiers HOTKEY_MOD_ALT   = 0x02;
constexpr AppHotkeyModifiers HOTKEY_MOD_SHIFT = 0x04;
constexpr AppHotkeyModifiers HOTKEY_MOD_WIN   = 0x08;

constexpr AppKeyCode APP_KEY_NONE      = 0x00;
constexpr AppKeyCode APP_KEY_BACKSPACE = 0x08;
constexpr AppKeyCode APP_KEY_TAB       = 0x09;
constexpr AppKeyCode APP_KEY_ENTER     = 0x0D;
constexpr AppKeyCode APP_KEY_SHIFT     = 0x10;
constexpr AppKeyCode APP_KEY_CONTROL   = 0x11;
constexpr AppKeyCode APP_KEY_ALT       = 0x12;
constexpr AppKeyCode APP_KEY_ESCAPE    = 0x1B;
constexpr AppKeyCode APP_KEY_SPACE     = 0x20;
constexpr AppKeyCode APP_KEY_PAGEUP    = 0x21;
constexpr AppKeyCode APP_KEY_PAGEDOWN  = 0x22;
constexpr AppKeyCode APP_KEY_END       = 0x23;
constexpr AppKeyCode APP_KEY_HOME      = 0x24;
constexpr AppKeyCode APP_KEY_LEFT      = 0x25;
constexpr AppKeyCode APP_KEY_UP        = 0x26;
constexpr AppKeyCode APP_KEY_RIGHT     = 0x27;
constexpr AppKeyCode APP_KEY_DOWN      = 0x28;
constexpr AppKeyCode APP_KEY_INSERT    = 0x2D;
constexpr AppKeyCode APP_KEY_DELETE    = 0x2E;
constexpr AppKeyCode APP_KEY_WIN       = 0x5B;
constexpr AppKeyCode APP_KEY_F1        = 0x70;
constexpr AppKeyCode APP_KEY_F2        = 0x71;
constexpr AppKeyCode APP_KEY_F3        = 0x72;
constexpr AppKeyCode APP_KEY_F24       = 0x87;

struct AudioInputDeviceInfo
{
	int Index;
	std::string Id;
	std::string Name;
	bool IsDefault;
};

struct PlatformFileInfo
{
	std::string Name;
	int64_t SizeBytes;
};

struct PlatformRuntimeState
{
	PlatformWindowHandle OwnWindow;
};

struct ColorRgba
{
	float R;
	float G;
	float B;
	float A;
};

struct HotkeyConfig
{
	AppHotkeyModifiers Modifiers;
	AppKeyCode VirtualKey;

	bool
	is_valid() const
	{
		return Modifiers != 0 || VirtualKey != APP_KEY_NONE;
	}
};

inline HotkeyConfig
default_record_hotkey()
{
	HotkeyConfig H = {};
	H.Modifiers = HOTKEY_MOD_CTRL | HOTKEY_MOD_ALT;
	H.VirtualKey = APP_KEY_NONE;
	return H;
}

inline HotkeyConfig
default_cancel_record_hotkey()
{
	HotkeyConfig H = {};
	H.Modifiers = HOTKEY_MOD_ALT;
	H.VirtualKey = APP_KEY_F3;
	return H;
}

inline HotkeyConfig
default_stream_hotkey()
{
	HotkeyConfig H = {};
	H.Modifiers = HOTKEY_MOD_ALT;
	H.VirtualKey = APP_KEY_F2;
	return H;
}

inline HotkeyConfig
default_load_model_hotkey()
{
	HotkeyConfig H = {};
	H.Modifiers = HOTKEY_MOD_ALT;
	H.VirtualKey = APP_KEY_F1;
	return H;
}

enum RecordingHotkeyMode
{
	RECORDING_HOTKEY_HOLD = 0,
	RECORDING_HOTKEY_TOGGLE = 1,
};

inline RecordingHotkeyMode
default_recording_hotkey_mode()
{
	return RECORDING_HOTKEY_HOLD;
}

inline bool
is_valid_recording_hotkey_mode(int Mode)
{
	return Mode == RECORDING_HOTKEY_HOLD || Mode == RECORDING_HOTKEY_TOGGLE;
}

enum ModelTransitionFailure
{
	MODEL_TRANSITION_FAILURE_NONE = 0,
	MODEL_TRANSITION_FAILURE_LOAD,
	MODEL_TRANSITION_FAILURE_RELOAD,
	MODEL_TRANSITION_FAILURE_TRANSFER,
};
