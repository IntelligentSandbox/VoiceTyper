#pragma once

#include "host_services.h"

#include <string>

inline bool
app_key_is_down(AppKeyCode Key)
{
	return platform_is_key_down(Key);
}

inline std::string
app_key_label(AppKeyCode Key)
{
	if (Key == APP_KEY_NONE)
		return "";

	if (Key >= APP_KEY_F1 && Key <= APP_KEY_F24)
		return "F" + std::to_string(Key - APP_KEY_F1 + 1);

	if (Key >= 'A' && Key <= 'Z')
		return std::string(1, (char)Key);

	if (Key >= '0' && Key <= '9')
		return std::string(1, (char)Key);

	switch (Key)
	{
	case APP_KEY_SPACE:     return "Space";
	case APP_KEY_ENTER:     return "Enter";
	case APP_KEY_ESCAPE:    return "Escape";
	case APP_KEY_TAB:       return "Tab";
	case APP_KEY_BACKSPACE: return "Backspace";
	case APP_KEY_DELETE:    return "Delete";
	case APP_KEY_INSERT:    return "Insert";
	case APP_KEY_HOME:      return "Home";
	case APP_KEY_END:       return "End";
	case APP_KEY_PAGEUP:    return "PageUp";
	case APP_KEY_PAGEDOWN:  return "PageDown";
	case APP_KEY_LEFT:      return "Left";
	case APP_KEY_RIGHT:     return "Right";
	case APP_KEY_UP:        return "Up";
	case APP_KEY_DOWN:      return "Down";
	default:                return "??";
	}
}

inline std::string
hotkey_to_label(const HotkeyConfig &Config)
{
	std::string Label;

	if (Config.Modifiers & HOTKEY_MOD_CTRL) Label += "Ctrl+";
	if (Config.Modifiers & HOTKEY_MOD_ALT) Label += "Alt+";
	if (Config.Modifiers & HOTKEY_MOD_SHIFT) Label += "Shift+";
	if (Config.Modifiers & HOTKEY_MOD_WIN) Label += "Win+";

	if (Config.VirtualKey == APP_KEY_NONE)
	{
		if (!Label.empty() && Label.back() == '+')
			Label.pop_back();
		return Label;
	}

	Label += app_key_label(Config.VirtualKey);
	return Label;
}

inline AppHotkeyModifiers
poll_modifier_state()
{
	AppHotkeyModifiers Mods = 0;
	if (app_key_is_down(APP_KEY_CONTROL)) Mods |= HOTKEY_MOD_CTRL;
	if (app_key_is_down(APP_KEY_ALT)) Mods |= HOTKEY_MOD_ALT;
	if (app_key_is_down(APP_KEY_SHIFT)) Mods |= HOTKEY_MOD_SHIFT;
	if (app_key_is_down(APP_KEY_WIN)) Mods |= HOTKEY_MOD_WIN;
	return Mods;
}

inline AppKeyCode
poll_nonmodifier_key()
{
	for (AppKeyCode Key = 'A'; Key <= 'Z'; Key++)
	{
		if (app_key_is_down(Key)) return Key;
	}
	for (AppKeyCode Key = '0'; Key <= '9'; Key++)
	{
		if (app_key_is_down(Key)) return Key;
	}
	for (AppKeyCode Key = APP_KEY_F1; Key <= APP_KEY_F24; Key++)
	{
		if (app_key_is_down(Key)) return Key;
	}

	AppKeyCode Specials[] = {
		APP_KEY_SPACE, APP_KEY_ENTER, APP_KEY_TAB, APP_KEY_BACKSPACE, APP_KEY_DELETE, APP_KEY_INSERT,
		APP_KEY_HOME, APP_KEY_END, APP_KEY_PAGEUP, APP_KEY_PAGEDOWN,
		APP_KEY_LEFT, APP_KEY_RIGHT, APP_KEY_UP, APP_KEY_DOWN
	};
	for (int i = 0; i < (int)(sizeof(Specials) / sizeof(Specials[0])); i++)
	{
		if (app_key_is_down(Specials[i])) return Specials[i];
	}
	return APP_KEY_NONE;
}

inline bool
check_modifier_state(AppHotkeyModifiers Modifiers)
{
	bool CtrlRequired  = (Modifiers & HOTKEY_MOD_CTRL)  != 0;
	bool AltRequired   = (Modifiers & HOTKEY_MOD_ALT)   != 0;
	bool ShiftRequired = (Modifiers & HOTKEY_MOD_SHIFT) != 0;
	bool WinRequired   = (Modifiers & HOTKEY_MOD_WIN)   != 0;

	bool CtrlDown  = app_key_is_down(APP_KEY_CONTROL);
	bool AltDown   = app_key_is_down(APP_KEY_ALT);
	bool ShiftDown = app_key_is_down(APP_KEY_SHIFT);
	bool WinDown   = app_key_is_down(APP_KEY_WIN);

	if (CtrlRequired  && !CtrlDown)  return false;
	if (AltRequired   && !AltDown)   return false;
	if (ShiftRequired && !ShiftDown) return false;
	if (WinRequired   && !WinDown)   return false;

	if (!CtrlRequired  && CtrlDown)  return false;
	if (!AltRequired   && AltDown)   return false;
	if (!ShiftRequired && ShiftDown) return false;

	return true;
}

inline bool
is_hotkey_down(const HotkeyConfig &Config)
{
	if (!check_modifier_state(Config.Modifiers)) return false;
	if (Config.VirtualKey == APP_KEY_NONE) return true;
	return app_key_is_down(Config.VirtualKey);
}
