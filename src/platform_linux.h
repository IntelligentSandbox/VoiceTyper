#pragma once

#include "app_icon.h"
#include "host_services.h"
#include "state.h"

#include <SDL.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <dlfcn.h>
#include <limits.h>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

#ifdef VOICETYPER_HAVE_X11
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <SDL_syswm.h>
#endif

inline std::vector<AudioInputDeviceInfo>
platform_query_audio_devices()
{
	std::vector<AudioInputDeviceInfo> Devices;

	if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0 && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
	{
		printf("[platform_linux] SDL audio init failed: %s\n", SDL_GetError());
		return Devices;
	}

	AudioInputDeviceInfo DefaultInfo = {};
	DefaultInfo.Index = 0;
	DefaultInfo.Id = "default";
	DefaultInfo.Name = "Default audio input";
	DefaultInfo.IsDefault = true;
	Devices.push_back(DefaultInfo);

	int NumDevices = SDL_GetNumAudioDevices(SDL_TRUE);
	for (int i = 0; i < NumDevices; i++)
	{
		const char *DeviceName = SDL_GetAudioDeviceName(i, SDL_TRUE);
		if (!DeviceName) continue;

		AudioInputDeviceInfo Info = {};
		Info.Index = i;
		Info.Id = std::to_string(i);
		Info.Name = DeviceName;
		Info.IsDefault = false;
		Devices.push_back(Info);
	}

	return Devices;
}

#ifdef VOICETYPER_HAVE_X11

typedef int (*LinuxX11ErrorHandlerFn)(Display *, void *);

struct LinuxX11ApiType
{
	void *X11Lib;
	void *XtstLib;
	Display *Dpy;
	bool Ready;
	bool XtstReady;

	Status (*XInitThreads)(void);
	Display *(*XOpenDisplay)(const char *);
	int (*XCloseDisplay)(Display *);
	Window (*XDefaultRootWindow)(Display *);
	Atom (*XInternAtom)(Display *, const char *, Bool);
	int (*XGetWindowProperty)(Display *, Window, Atom, long, long, Bool, Atom, Atom *, int *, unsigned long *,
		unsigned long *, unsigned char **);
	int (*XFree)(void *);
	int (*XGetInputFocus)(Display *, Window *, int *);
	int (*XSetInputFocus)(Display *, Window, int, Time);
	int (*XRaiseWindow)(Display *, Window);
	KeyCode (*XKeysymToKeycode)(Display *, KeySym);
	KeySym *(*XGetKeyboardMapping)(Display *, KeyCode, int, int *);
	int (*XChangeKeyboardMapping)(Display *, int, int, KeySym *, int);
	int (*XDisplayKeycodes)(Display *, int *, int *);
	int (*XSendEvent)(Display *, Window, Bool, long, XEvent *);
	int (*XSync)(Display *, Bool);
	int (*XFlush)(Display *);
	LinuxX11ErrorHandlerFn (*XSetErrorHandler)(LinuxX11ErrorHandlerFn);
	Status (*XGetWindowAttributes)(Display *, Window, XWindowAttributes *);
	int (*XTestFakeKeyEvent)(Display *, unsigned int, Bool, unsigned long);
};

static LinuxX11ApiType g_LinuxX11Api = {};
static std::once_flag g_LinuxX11Once;
static std::mutex g_LinuxX11Mutex;

static int
linux_x11_ignore_error(Display *, void *)
{
	return 0;
}

static void
linux_x11_init_once()
{
	void *X11Lib = dlopen("libX11.so.6", RTLD_LAZY | RTLD_LOCAL);
	if (!X11Lib)
	{
		return;
	}

	LinuxX11ApiType Api = {};
	Api.X11Lib = X11Lib;
	Api.XInitThreads = reinterpret_cast<Status (*)(void)>(dlsym(X11Lib, "XInitThreads"));
	Api.XOpenDisplay = reinterpret_cast<Display *(*)(const char *)>(dlsym(X11Lib, "XOpenDisplay"));
	Api.XCloseDisplay = reinterpret_cast<int (*)(Display *)>(dlsym(X11Lib, "XCloseDisplay"));
	Api.XDefaultRootWindow = reinterpret_cast<Window (*)(Display *)>(dlsym(X11Lib, "XDefaultRootWindow"));
	Api.XInternAtom = reinterpret_cast<Atom (*)(Display *, const char *, Bool)>(dlsym(X11Lib, "XInternAtom"));
	Api.XGetWindowProperty = reinterpret_cast<int (*)(Display *, Window, Atom, long, long, Bool, Atom, Atom *, int *,
		unsigned long *, unsigned long *, unsigned char **)>(dlsym(X11Lib, "XGetWindowProperty"));
	Api.XFree = reinterpret_cast<int (*)(void *)>(dlsym(X11Lib, "XFree"));
	Api.XGetInputFocus = reinterpret_cast<int (*)(Display *, Window *, int *)>(dlsym(X11Lib, "XGetInputFocus"));
	Api.XSetInputFocus = reinterpret_cast<int (*)(Display *, Window, int, Time)>(dlsym(X11Lib, "XSetInputFocus"));
	Api.XRaiseWindow = reinterpret_cast<int (*)(Display *, Window)>(dlsym(X11Lib, "XRaiseWindow"));
	Api.XKeysymToKeycode = reinterpret_cast<KeyCode (*)(Display *, KeySym)>(dlsym(X11Lib, "XKeysymToKeycode"));
	Api.XGetKeyboardMapping = reinterpret_cast<KeySym *(*)(Display *, KeyCode, int, int *)>(
		dlsym(X11Lib, "XGetKeyboardMapping"));
	Api.XChangeKeyboardMapping = reinterpret_cast<int (*)(Display *, int, int, KeySym *, int)>(
		dlsym(X11Lib, "XChangeKeyboardMapping"));
	Api.XDisplayKeycodes = reinterpret_cast<int (*)(Display *, int *, int *)>(dlsym(X11Lib, "XDisplayKeycodes"));
	Api.XSendEvent = reinterpret_cast<int (*)(Display *, Window, Bool, long, XEvent *)>(dlsym(X11Lib, "XSendEvent"));
	Api.XSync = reinterpret_cast<int (*)(Display *, Bool)>(dlsym(X11Lib, "XSync"));
	Api.XFlush = reinterpret_cast<int (*)(Display *)>(dlsym(X11Lib, "XFlush"));
	Api.XSetErrorHandler = reinterpret_cast<LinuxX11ErrorHandlerFn (*)(LinuxX11ErrorHandlerFn)>(
		dlsym(X11Lib, "XSetErrorHandler"));
	Api.XGetWindowAttributes = reinterpret_cast<Status (*)(Display *, Window, XWindowAttributes *)>(
		dlsym(X11Lib, "XGetWindowAttributes"));

	if (!Api.XInitThreads || !Api.XOpenDisplay || !Api.XDefaultRootWindow || !Api.XInternAtom || !Api.XGetWindowProperty ||
		!Api.XFree || !Api.XGetInputFocus || !Api.XSetInputFocus || !Api.XRaiseWindow || !Api.XKeysymToKeycode ||
		!Api.XGetKeyboardMapping || !Api.XChangeKeyboardMapping || !Api.XDisplayKeycodes ||
		!Api.XSendEvent || !Api.XSync || !Api.XSetErrorHandler || !Api.XGetWindowAttributes)
	{
		dlclose(X11Lib);
		return;
	}

	if (!Api.XInitThreads())
	{
		dlclose(X11Lib);
		return;
	}

	Api.Dpy = Api.XOpenDisplay(nullptr);
	if (!Api.Dpy)
	{
		dlclose(X11Lib);
		return;
	}

	void *XtstLib = dlopen("libXtst.so.6", RTLD_LAZY | RTLD_LOCAL);
	if (XtstLib)
	{
		Api.XtstLib = XtstLib;
		Api.XTestFakeKeyEvent = reinterpret_cast<int (*)(Display *, unsigned int, Bool, unsigned long)>(
			dlsym(XtstLib, "XTestFakeKeyEvent"));
		if (!Api.XTestFakeKeyEvent)
		{
			dlclose(XtstLib);
			Api.XtstLib = nullptr;
		}
	}
	Api.XtstReady = Api.XTestFakeKeyEvent != nullptr;

	Api.Ready = true;
	g_LinuxX11Api = Api;
}

static LinuxX11ApiType *
linux_x11_acquire(std::unique_lock<std::mutex> &Lock)
{
	Lock = std::unique_lock<std::mutex>(g_LinuxX11Mutex);
	std::call_once(g_LinuxX11Once, linux_x11_init_once);
	if (!g_LinuxX11Api.Ready)
	{
		return nullptr;
	}
	return &g_LinuxX11Api;
}

static KeySym
linux_app_key_to_keysym(AppKeyCode Key)
{
	if (Key >= 'A' && Key <= 'Z')
	{
		return XK_a + (Key - 'A');
	}
	if (Key >= '0' && Key <= '9')
	{
		return XK_0 + (Key - '0');
	}
	if (Key >= APP_KEY_F1 && Key <= APP_KEY_F24)
	{
		return XK_F1 + (Key - APP_KEY_F1);
	}

	switch (Key)
	{
	case APP_KEY_SPACE:     return XK_space;
	case APP_KEY_ENTER:     return XK_Return;
	case APP_KEY_ESCAPE:    return XK_Escape;
	case APP_KEY_TAB:       return XK_Tab;
	case APP_KEY_BACKSPACE: return XK_BackSpace;
	case APP_KEY_DELETE:    return XK_Delete;
	case APP_KEY_INSERT:    return XK_Insert;
	case APP_KEY_HOME:      return XK_Home;
	case APP_KEY_END:       return XK_End;
	case APP_KEY_PAGEUP:    return XK_Prior;
	case APP_KEY_PAGEDOWN:  return XK_Next;
	case APP_KEY_LEFT:      return XK_Left;
	case APP_KEY_RIGHT:     return XK_Right;
	case APP_KEY_UP:        return XK_Up;
	case APP_KEY_DOWN:      return XK_Down;
	default:                return 0;
	}
}

static KeySym
linux_utf8_to_keysym(uint32_t Codepoint)
{
	if (Codepoint == '\n')
	{
		return XK_Return;
	}
	if (Codepoint == '\t')
	{
		return XK_Tab;
	}
	return 0x01000000u | Codepoint;
}

static uint32_t
linux_utf8_next_codepoint(const char *&Text)
{
	unsigned char Lead = (unsigned char)*Text;
	if (Lead == 0)
	{
		return 0;
	}

	uint32_t Codepoint = 0;
	int Extra = 0;
	if (Lead < 0x80)
	{
		Codepoint = Lead;
	}
	else if ((Lead & 0xE0) == 0xC0)
	{
		Codepoint = Lead & 0x1F;
		Extra = 1;
	}
	else if ((Lead & 0xF0) == 0xE0)
	{
		Codepoint = Lead & 0x0F;
		Extra = 2;
	}
	else if ((Lead & 0xF8) == 0xF0)
	{
		Codepoint = Lead & 0x07;
		Extra = 3;
	}
	else
	{
		Text++;
		return 0xFFFD;
	}

	Text++;
	for (int i = 0; i < Extra; i++)
	{
		unsigned char Cont = (unsigned char)*Text;
		if ((Cont & 0xC0) != 0x80)
		{
			return 0xFFFD;
		}
		Codepoint = (Codepoint << 6) | (Cont & 0x3F);
		Text++;
	}

	if ((Extra == 1 && Codepoint < 0x80) || (Extra == 2 && Codepoint < 0x800) || (Extra == 3 && Codepoint < 0x10000) ||
		Codepoint > 0x10FFFF || (Codepoint >= 0xD800 && Codepoint <= 0xDFFF))
	{
		return 0xFFFD;
	}

	return Codepoint;
}

static void
linux_x11_activate_window(const LinuxX11ApiType &Api, Display *Dpy, Window Root, Window W)
{
	LinuxX11ErrorHandlerFn Prev = Api.XSetErrorHandler(&linux_x11_ignore_error);

	Api.XRaiseWindow(Dpy, W);

	XClientMessageEvent Ev = {};
	Ev.type = ClientMessage;
	Ev.serial = 0;
	Ev.send_event = True;
	Ev.display = Dpy;
	Ev.window = Root;
	Ev.message_type = Api.XInternAtom(Dpy, "_NET_ACTIVE_WINDOW", False);
	Ev.format = 32;
	Ev.data.l[0] = 2;
	Api.XSendEvent(Dpy, Root, False, SubstructureRedirectMask | SubstructureNotifyMask, (XEvent *)&Ev);

	Api.XSetInputFocus(Dpy, W, RevertToPointerRoot, CurrentTime);

	Api.XSync(Dpy, False);
	Api.XSetErrorHandler(Prev);
}

#endif

inline void
platform_inject_text(PlatformRuntimeState *Platform, void *Window, const char *Utf8, bool CharByChar,
	HotkeyConfig PasteHotkey)
{
	if (!Utf8 || Utf8[0] == '\0')
	{
		return;
	}
	if (!Window)
	{
		return;
	}
	if (Platform && Window == Platform->OwnWindow)
	{
		return;
	}

#ifdef VOICETYPER_HAVE_X11
	std::unique_lock<std::mutex> X11Lock;
	LinuxX11ApiType *Api = linux_x11_acquire(X11Lock);
	if (Api && Api->XtstReady)
	{
		Display *Dpy = Api->Dpy;
		::Window XID = (::Window)(uintptr_t)Window;
		::Window Root = Api->XDefaultRootWindow(Dpy);

		if (CharByChar)
		{
			std::vector<KeyCode> Codes;
			std::vector<KeySym> Syms;
			bool AllMapped = true;
			const char *Cursor = Utf8;
			for (;;)
			{
				uint32_t Codepoint = linux_utf8_next_codepoint(Cursor);
				if (Codepoint == 0)
				{
					break;
				}

				KeySym Sym = linux_utf8_to_keysym(Codepoint);
				KeyCode Code = Api->XKeysymToKeycode(Dpy, Sym);
				if (Code == 0)
				{
					AllMapped = false;
				}
				Syms.push_back(Sym);
				Codes.push_back(Code);
			}

			if (!Syms.empty() && AllMapped)
			{
				linux_x11_activate_window(*Api, Dpy, Root, XID);
				usleep(50000);

				LinuxX11ErrorHandlerFn Prev = Api->XSetErrorHandler(&linux_x11_ignore_error);
				for (KeyCode Code : Codes)
				{
					Api->XTestFakeKeyEvent(Dpy, (unsigned int)Code, True, 0);
					usleep(1000);
					Api->XTestFakeKeyEvent(Dpy, (unsigned int)Code, False, 0);
					usleep(1000);
				}
				Api->XSync(Dpy, False);
				Api->XSetErrorHandler(Prev);
				return;
			}

			if (!Syms.empty())
			{
				int MinCode = 0;
				int MaxCode = 0;
				if (Api->XDisplayKeycodes(Dpy, &MinCode, &MaxCode) && MaxCode >= MinCode && MinCode > 0)
				{
					KeyCode Spare = (KeyCode)MaxCode;
					int SymsPerCode = 0;
					KeySym *Original = Api->XGetKeyboardMapping(Dpy, Spare, 1, &SymsPerCode);
					if (Original && SymsPerCode > 0)
					{
						linux_x11_activate_window(*Api, Dpy, Root, XID);
						Api->XChangeKeyboardMapping(Dpy, (int)Spare, 1, &Syms[0], 1);
						usleep(50000);

						LinuxX11ErrorHandlerFn Prev = Api->XSetErrorHandler(&linux_x11_ignore_error);
						for (size_t i = 0; i < Syms.size(); i++)
						{
							if (i > 0)
							{
								Api->XChangeKeyboardMapping(Dpy, (int)Spare, 1, &Syms[i], 1);
								Api->XSync(Dpy, False);
								usleep(5000);
							}
							Api->XTestFakeKeyEvent(Dpy, (unsigned int)Spare, True, 0);
							Api->XFlush(Dpy);
							usleep(2000);
							Api->XTestFakeKeyEvent(Dpy, (unsigned int)Spare, False, 0);
							Api->XFlush(Dpy);
							usleep(12000);
						}
						Api->XChangeKeyboardMapping(Dpy, (int)Spare, SymsPerCode, Original, 1);
						Api->XSync(Dpy, False);
						Api->XSetErrorHandler(Prev);
						Api->XFree(Original);
						return;
					}
					if (Original)
					{
						Api->XFree(Original);
					}
				}
			}
		}

		char *PreviousClipboard = SDL_GetClipboardText();
		bool HadPreviousText = PreviousClipboard && PreviousClipboard[0] != '\0';
		std::string PreviousText = HadPreviousText ? std::string(PreviousClipboard) : std::string();
		bool CanRestoreClipboard = PreviousClipboard != nullptr;
		if (PreviousClipboard) SDL_free(PreviousClipboard);

		SDL_SetClipboardText(Utf8);
		linux_x11_activate_window(*Api, Dpy, Root, XID);
		usleep(50000);

		KeyCode ModCodes[4];
		int ModCount = 0;
		if (PasteHotkey.Modifiers & HOTKEY_MOD_CTRL)
		{
			KeyCode Code = Api->XKeysymToKeycode(Dpy, XK_Control_L);
			if (Code != 0)
			{
				ModCodes[ModCount++] = Code;
			}
		}
		if (PasteHotkey.Modifiers & HOTKEY_MOD_ALT)
		{
			KeyCode Code = Api->XKeysymToKeycode(Dpy, XK_Alt_L);
			if (Code != 0)
			{
				ModCodes[ModCount++] = Code;
			}
		}
		if (PasteHotkey.Modifiers & HOTKEY_MOD_SHIFT)
		{
			KeyCode Code = Api->XKeysymToKeycode(Dpy, XK_Shift_L);
			if (Code != 0)
			{
				ModCodes[ModCount++] = Code;
			}
		}
		if (PasteHotkey.Modifiers & HOTKEY_MOD_WIN)
		{
			KeyCode Code = Api->XKeysymToKeycode(Dpy, XK_Super_L);
			if (Code != 0)
			{
				ModCodes[ModCount++] = Code;
			}
		}

		KeyCode Key = 0;
		if (PasteHotkey.VirtualKey != APP_KEY_NONE)
		{
			Key = Api->XKeysymToKeycode(Dpy, linux_app_key_to_keysym(PasteHotkey.VirtualKey));
			if (Key == 0)
			{
				return;
			}
		}

		LinuxX11ErrorHandlerFn Prev = Api->XSetErrorHandler(&linux_x11_ignore_error);
		for (int i = 0; i < ModCount; i++)
		{
			Api->XTestFakeKeyEvent(Dpy, (unsigned int)ModCodes[i], True, 0);
			usleep(10000);
		}
		if (Key != 0)
		{
			Api->XTestFakeKeyEvent(Dpy, (unsigned int)Key, True, 0);
			usleep(10000);
			Api->XTestFakeKeyEvent(Dpy, (unsigned int)Key, False, 0);
			usleep(10000);
		}
		for (int i = ModCount - 1; i >= 0; i--)
		{
			Api->XTestFakeKeyEvent(Dpy, (unsigned int)ModCodes[i], False, 0);
			usleep(10000);
		}
		Api->XSync(Dpy, False);
		Api->XSetErrorHandler(Prev);

		if (CanRestoreClipboard)
		{
			usleep(200000);

			char *CurrentClipboard = SDL_GetClipboardText();
			bool Unchanged = CurrentClipboard && strcmp(CurrentClipboard, Utf8) == 0;
			if (CurrentClipboard) SDL_free(CurrentClipboard);
			if (Unchanged) SDL_SetClipboardText(PreviousText.c_str());
		}
		return;
	}
#else
	(void)CharByChar;
	(void)PasteHotkey;
#endif

	SDL_SetClipboardText(Utf8);
}

inline void
platform_set_clipboard_text(PlatformRuntimeState *Platform, const char *Utf8)
{
	(void)Platform;
	if (!Utf8 || Utf8[0] == '\0') return;

	SDL_SetClipboardText(Utf8);
}

inline void *
platform_get_foreground_window(PlatformRuntimeState *Platform)
{
#ifdef VOICETYPER_HAVE_X11
	std::unique_lock<std::mutex> X11Lock;
	LinuxX11ApiType *Api = linux_x11_acquire(X11Lock);
	if (!Api)
	{
		return nullptr;
	}

	Display *Dpy = Api->Dpy;
	Window Root = Api->XDefaultRootWindow(Dpy);
	Window Active = None;

	LinuxX11ErrorHandlerFn Prev = Api->XSetErrorHandler(&linux_x11_ignore_error);

	Atom NetActive = Api->XInternAtom(Dpy, "_NET_ACTIVE_WINDOW", True);
	if (NetActive != None)
	{
		Atom PropType = None;
		int PropFormat = 0;
		unsigned long NumItems = 0;
		unsigned long BytesAfter = 0;
		unsigned char *Data = nullptr;
		int Ret = Api->XGetWindowProperty(Dpy, Root, NetActive, 0, 1, False, XA_WINDOW, &PropType, &PropFormat,
			&NumItems, &BytesAfter, &Data);
		if (Ret == Success && PropType == XA_WINDOW && NumItems >= 1 && Data)
		{
			Active = (Window)((long *)Data)[0];
		}
		if (Data)
		{
			Api->XFree(Data);
		}
	}

	if (Active == None || Active == Root)
	{
		Window Focus = None;
		int RevertTo = 0;
		if (Api->XGetInputFocus(Dpy, &Focus, &RevertTo) && Focus != None && Focus != PointerRoot && Focus != Root)
		{
			Active = Focus;
		}
	}

	Api->XSync(Dpy, False);
	Api->XSetErrorHandler(Prev);

	if (Active == None || Active == Root)
	{
		return nullptr;
	}

	if (Platform && Platform->OwnWindow)
	{
#if defined(SDL_VIDEO_DRIVER_X11)
		SDL_Window *SdlWindow = (SDL_Window *)Platform->OwnWindow;
		SDL_SysWMinfo Info;
		SDL_VERSION(&Info.version);
		if (SDL_GetWindowWMInfo(SdlWindow, &Info) && Info.subsystem == SDL_SYSWM_X11 && Active == Info.info.x11.window)
		{
			return Platform->OwnWindow;
		}
#endif
	}

	return (void *)(uintptr_t)Active;
#else
	(void)Platform;
	return nullptr;
#endif
}

inline std::string
platform_get_window_process_name(void *Window)
{
#ifdef VOICETYPER_HAVE_X11
	if (!Window)
	{
		return "";
	}

	std::unique_lock<std::mutex> X11Lock;
	LinuxX11ApiType *Api = linux_x11_acquire(X11Lock);
	if (!Api)
	{
		return "";
	}

	Display *Dpy = Api->Dpy;
	::Window XID = (::Window)(uintptr_t)Window;

	long Pid = 0;
	LinuxX11ErrorHandlerFn Prev = Api->XSetErrorHandler(&linux_x11_ignore_error);

	Atom NetWmPid = Api->XInternAtom(Dpy, "_NET_WM_PID", True);
	if (NetWmPid != None)
	{
		Atom PropType = None;
		int PropFormat = 0;
		unsigned long NumItems = 0;
		unsigned long BytesAfter = 0;
		unsigned char *Data = nullptr;
		int Ret = Api->XGetWindowProperty(Dpy, XID, NetWmPid, 0, 1, False, XA_CARDINAL, &PropType, &PropFormat,
			&NumItems, &BytesAfter, &Data);
		if (Ret == Success && PropType == XA_CARDINAL && PropFormat == 32 && NumItems >= 1 && Data)
		{
			Pid = (long)((unsigned long *)Data)[0];
		}
		if (Data)
		{
			Api->XFree(Data);
		}
	}

	Api->XSync(Dpy, False);
	Api->XSetErrorHandler(Prev);

	if (Pid <= 0)
	{
		return "";
	}

	char Comm[256] = {};
	std::string ProcPath = "/proc/" + std::to_string(Pid) + "/comm";
	FILE *F = fopen(ProcPath.c_str(), "r");
	if (!F)
	{
		return "";
	}
	if (fgets(Comm, sizeof(Comm), F))
	{
		size_t Len = strlen(Comm);
		while (Len > 0 && (Comm[Len - 1] == '\n' || Comm[Len - 1] == '\r'))
		{
			Comm[--Len] = '\0';
		}
	}
	fclose(F);
	return std::string(Comm);
#else
	(void)Window;
	return "";
#endif
}

inline void
platform_set_taskbar_icon(void *Window, const char *PngPath)
{
	(void)PngPath;
	if (!Window)
	{
		return;
	}

	SDL_Surface *Icon = SDL_CreateRGBSurfaceWithFormatFrom((void *)APP_ICON_EMBEDDED_RGBA, APP_ICON_EMBEDDED_WIDTH,
		APP_ICON_EMBEDDED_HEIGHT, 32, APP_ICON_EMBEDDED_WIDTH * 4, SDL_PIXELFORMAT_RGBA32);
	if (!Icon)
	{
		return;
	}

	SDL_SetWindowIcon((SDL_Window *)Window, Icon);
	SDL_FreeSurface(Icon);
}

inline void
platform_apply_window_theme(void *Window, bool LightMode)
{
	(void)Window;
	(void)LightMode;
}

inline void
platform_play_sound(PlatformRuntimeState *Platform, int FreqHz, int DurationMs)
{
	(void)Platform;
	if (DurationMs <= 0)
	{
		return;
	}
	if (FreqHz < 20)
	{
		return;
	}

	const int FixedVolume = SOUND_DEFAULT_VOLUME;
	if (FixedVolume <= 0)
	{
		return;
	}

	const int SampleRate = 44100;
	const int NumSamples = (SampleRate * DurationMs) / 1000;
	const int AttackMs = 8;
	const int AttackSamples = (SampleRate * AttackMs) / 1000;

	std::thread([FreqHz, DurationMs, FixedVolume, SampleRate, NumSamples, AttackSamples]() {
		if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0 && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
		{
			return;
		}

		SDL_AudioSpec Desired = {};
		Desired.freq = SampleRate;
		Desired.format = AUDIO_S16SYS;
		Desired.channels = 1;
		Desired.samples = 4096;
		Desired.callback = nullptr;
		Desired.userdata = nullptr;

		SDL_AudioSpec Obtained = {};
		SDL_AudioDeviceID Device = SDL_OpenAudioDevice(nullptr, SDL_FALSE, &Desired, &Obtained, 0);
		if (Device == 0)
		{
			return;
		}

		std::vector<int16_t> Buffer((size_t)NumSamples);

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
			float Sample = sinf(Pi2 * FreqHz * t) + Harmonic2Gain * sinf(Pi2 * 2.0f * FreqHz * t) +
				Harmonic3Gain * sinf(Pi2 * 3.0f * FreqHz * t);
			Sample *= Norm;

			Amp *= DecayPerSample;
			float Env = Amp;
			if (i < AttackSamples)
			{
				Env *= (float)i / (float)AttackSamples;
			}

			Sample *= PeakAmplitude * Env;
			if (Sample > 32767.0f)
			{
				Sample = 32767.0f;
			}
			if (Sample < -32768.0f)
			{
				Sample = -32768.0f;
			}
			Buffer[(size_t)i] = (int16_t)Sample;
		}

		SDL_ClearQueuedAudio(Device);
		SDL_QueueAudio(Device, Buffer.data(), (Uint32)(Buffer.size() * sizeof(int16_t)));
		SDL_PauseAudioDevice(Device, 0);

		while (SDL_GetQueuedAudioSize(Device) > 0)
		{
			SDL_Delay(10);
		}

		SDL_CloseAudioDevice(Device);
	}).detach();
}

static bool
platform_is_sdl_scancode_down(SDL_Scancode Scan)
{
	int NumKeys = 0;
	const Uint8 *Keys = SDL_GetKeyboardState(&NumKeys);
	if (!Keys || Scan == SDL_SCANCODE_UNKNOWN || Scan >= NumKeys) return false;

	return Keys[Scan] != 0;
}

inline bool
platform_is_key_down(AppKeyCode Key)
{
	if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0) return false;

	if (Key == APP_KEY_SHIFT)
	{
		return platform_is_sdl_scancode_down(SDL_SCANCODE_LSHIFT) ||
			platform_is_sdl_scancode_down(SDL_SCANCODE_RSHIFT);
	}
	if (Key == APP_KEY_CONTROL)
	{
		return platform_is_sdl_scancode_down(SDL_SCANCODE_LCTRL) ||
			platform_is_sdl_scancode_down(SDL_SCANCODE_RCTRL);
	}
	if (Key == APP_KEY_ALT)
	{
		return platform_is_sdl_scancode_down(SDL_SCANCODE_LALT) ||
			platform_is_sdl_scancode_down(SDL_SCANCODE_RALT);
	}
	if (Key == APP_KEY_WIN)
	{
		return platform_is_sdl_scancode_down(SDL_SCANCODE_LGUI) ||
			platform_is_sdl_scancode_down(SDL_SCANCODE_RGUI);
	}

	SDL_Scancode Scan = SDL_SCANCODE_UNKNOWN;
	if (Key >= 'A' && Key <= 'Z') Scan = (SDL_Scancode)(SDL_SCANCODE_A + (Key - 'A'));
	else if (Key >= APP_KEY_F1 && Key <= APP_KEY_F24) Scan = (SDL_Scancode)(SDL_SCANCODE_F1 + (Key - APP_KEY_F1));
	else
	{
		switch (Key)
		{
		case '0': Scan = SDL_SCANCODE_0; break;
		case '1': Scan = SDL_SCANCODE_1; break;
		case '2': Scan = SDL_SCANCODE_2; break;
		case '3': Scan = SDL_SCANCODE_3; break;
		case '4': Scan = SDL_SCANCODE_4; break;
		case '5': Scan = SDL_SCANCODE_5; break;
		case '6': Scan = SDL_SCANCODE_6; break;
		case '7': Scan = SDL_SCANCODE_7; break;
		case '8': Scan = SDL_SCANCODE_8; break;
		case '9': Scan = SDL_SCANCODE_9; break;
		case APP_KEY_SPACE:     Scan = SDL_SCANCODE_SPACE; break;
		case APP_KEY_ENTER:     Scan = SDL_SCANCODE_RETURN; break;
		case APP_KEY_ESCAPE:    Scan = SDL_SCANCODE_ESCAPE; break;
		case APP_KEY_TAB:       Scan = SDL_SCANCODE_TAB; break;
		case APP_KEY_BACKSPACE: Scan = SDL_SCANCODE_BACKSPACE; break;
		case APP_KEY_DELETE:    Scan = SDL_SCANCODE_DELETE; break;
		case APP_KEY_INSERT:    Scan = SDL_SCANCODE_INSERT; break;
		case APP_KEY_HOME:      Scan = SDL_SCANCODE_HOME; break;
		case APP_KEY_END:       Scan = SDL_SCANCODE_END; break;
		case APP_KEY_PAGEUP:    Scan = SDL_SCANCODE_PAGEUP; break;
		case APP_KEY_PAGEDOWN:  Scan = SDL_SCANCODE_PAGEDOWN; break;
		case APP_KEY_LEFT:      Scan = SDL_SCANCODE_LEFT; break;
		case APP_KEY_RIGHT:     Scan = SDL_SCANCODE_RIGHT; break;
		case APP_KEY_UP:        Scan = SDL_SCANCODE_UP; break;
		case APP_KEY_DOWN:      Scan = SDL_SCANCODE_DOWN; break;
		default: break;
		}
	}

	return platform_is_sdl_scancode_down(Scan);
}

inline std::string
platform_get_binary_path()
{
	char ExePath[PATH_MAX] = {};
	ssize_t Len = readlink("/proc/self/exe", ExePath, sizeof(ExePath) - 1);
	if (Len <= 0) return "";

	ExePath[Len] = '\0';
	return std::string(ExePath);
}

inline std::string
platform_get_binary_dir()
{
	std::string ExePath = platform_get_binary_path();
	size_t LastSlash = ExePath.find_last_of('/');
	if (LastSlash != std::string::npos) ExePath.resize(LastSlash);
	return ExePath;
}

inline std::string
platform_get_data_dir()
{
	const char *DataDir = getenv("VOICETYPER_DATA_DIR");
	if (DataDir && DataDir[0] != '\0') return std::string(DataDir);

	return platform_get_binary_dir();
}

inline bool
platform_ensure_directory(const std::string &Path)
{
	if (Path.empty()) return false;

	std::string Normalized = Path;
	for (char &Ch : Normalized)
	{
		if (Ch == '\\') Ch = '/';
	}

	if (Normalized == "/") return true;

	size_t Pos = (Normalized[0] == '/') ? 1 : 0;
	for (;;)
	{
		Pos = Normalized.find('/', Pos);
		std::string Partial = (Pos == std::string::npos) ? Normalized : Normalized.substr(0, Pos);

		if (!Partial.empty() && mkdir(Partial.c_str(), 0755) != 0 && errno != EEXIST) return false;

		if (Pos == std::string::npos) break;
		Pos++;
	}

	return true;
}

inline bool
platform_remove_empty_directory(const std::string &Path)
{
	if (Path.empty()) return false;
	return rmdir(Path.c_str()) == 0;
}

inline std::vector<PlatformFileInfo>
platform_list_files(const std::string &Dir)
{
	std::vector<PlatformFileInfo> Files;
	DIR *Directory = opendir(Dir.c_str());
	if (!Directory) return Files;

	for (;;)
	{
		dirent *Entry = readdir(Directory);
		if (!Entry) break;
		if (strcmp(Entry->d_name, ".") == 0 || strcmp(Entry->d_name, "..") == 0) continue;

		std::string FilePath = platform_join_path(Dir, Entry->d_name);
		struct stat Stat = {};
		if (stat(FilePath.c_str(), &Stat) != 0) continue;
		if (S_ISDIR(Stat.st_mode)) continue;

		PlatformFileInfo Info = {};
		Info.Name = Entry->d_name;
		Info.SizeBytes = Stat.st_size;
		Files.push_back(Info);
	}

	closedir(Directory);
	return Files;
}

struct SdlCaptureContext
{
	GlobalState *AppState;
};

static void
platform_sdl_capture_callback(void *UserData, Uint8 *Stream, int Len)
{
	SdlCaptureContext *Context = (SdlCaptureContext *)UserData;
	if (!Context || !Context->AppState || !Stream || Len <= 0) return;

	int SampleCount = Len / (int)sizeof(int16_t);
	const int16_t *Samples = (const int16_t *)Stream;

	std::lock_guard<std::mutex> Lock(Context->AppState->AudioBufferMutex);
	size_t OldSize = Context->AppState->AudioAccumBuffer.size();
	Context->AppState->AudioAccumBuffer.resize(OldSize + SampleCount);
	for (int i = 0; i < SampleCount; i++)
	{
		Context->AppState->AudioAccumBuffer[OldSize + i] = Samples[i] / 32768.0f;
	}
}

inline bool
platform_audio_capture(PlatformRuntimeState *Platform, GlobalState *AppState, int DeviceIndex)
{
	(void)Platform;
	if (!AppState || DeviceIndex < 0 || DeviceIndex >= (int)AppState->AudioInputDevices.size()) return false;

	if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0 && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
	{
		printf("[audio_pipeline] ERROR: SDL audio init failed: %s\n", SDL_GetError());
		return false;
	}

	const AudioInputDeviceInfo &DeviceInfo = AppState->AudioInputDevices[DeviceIndex];
	const char *DeviceName = nullptr;
	if (DeviceInfo.Id != "default") DeviceName = DeviceInfo.Name.c_str();

	SdlCaptureContext Context = {};
	Context.AppState = AppState;

	SDL_AudioSpec Desired = {};
	Desired.freq = AUDIO_CAPTURE_SAMPLE_RATE;
	Desired.format = AUDIO_S16SYS;
	Desired.channels = AUDIO_CAPTURE_CHANNELS;
	Desired.samples = (AUDIO_CAPTURE_SAMPLE_RATE * AUDIO_CAPTURE_BUFFER_MS) / 1000;
	Desired.callback = platform_sdl_capture_callback;
	Desired.userdata = &Context;

	SDL_AudioSpec Obtained = {};
	SDL_AudioDeviceID Device = SDL_OpenAudioDevice(DeviceName, SDL_TRUE, &Desired, &Obtained, 0);
	if (Device == 0)
	{
		printf("[audio_pipeline] ERROR: SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
		return false;
	}

	SDL_PauseAudioDevice(Device, 0);
	while (AppState->CaptureRunning.load()) SDL_Delay(20);
	SDL_PauseAudioDevice(Device, 1);
	SDL_CloseAudioDevice(Device);

	return true;
}

inline bool
platform_spawn_detached(const std::string &CommandLine, const std::string &WorkingDir, bool Hidden)
{
	(void)Hidden;

	pid_t Pid = fork();
	if (Pid < 0)
	{
		return false;
	}
	if (Pid > 0)
	{
		return true;
	}

	setsid();
	if (!WorkingDir.empty())
	{
		chdir(WorkingDir.c_str());
	}
	execlp("sh", "sh", "-c", CommandLine.c_str(), (char *)nullptr);
	_exit(127);
}

inline bool
platform_is_installed_build()
{
	return false;
}

inline int
platform_get_process_id()
{
	return (int)getpid();
}

inline std::string
platform_get_temp_dir()
{
	return "/tmp";
}

inline void
platform_open_url(const char *Url)
{
	pid_t Pid = fork();
	if (Pid != 0)
	{
		return;
	}

	execlp("xdg-open", "xdg-open", Url, (char *)nullptr);
	_exit(127);
}

static std::string
linux_string_to_lower(const std::string &Text)
{
	std::string Lower = Text;
	for (char &Ch : Lower)
	{
		if (Ch >= 'A' && Ch <= 'Z')
		{
			Ch = (char)(Ch - 'A' + 'a');
		}
	}
	return Lower;
}

static bool
linux_font_is_font_file(const std::string &FileName)
{
	if (FileName.size() < 4)
	{
		return false;
	}

	std::string Extension = linux_string_to_lower(FileName.substr(FileName.size() - 4));
	return Extension == ".ttf" || Extension == ".otf" || Extension == ".ttc";
}

static void
linux_font_push_dir(std::vector<std::string> &Dirs, const std::string &Dir)
{
	if (Dir.empty())
	{
		return;
	}

	for (const std::string &Existing : Dirs)
	{
		if (Existing == Dir)
		{
			return;
		}
	}

	struct stat Stat = {};
	if (stat(Dir.c_str(), &Stat) != 0 || !S_ISDIR(Stat.st_mode))
	{
		return;
	}

	Dirs.push_back(Dir);
}

static void
linux_font_scan_dir(const std::string &Dir, int Depth, std::vector<PlatformFontInfo> &Fonts)
{
	if (Depth >= 8)
	{
		return;
	}

	DIR *Directory = opendir(Dir.c_str());
	if (!Directory)
	{
		return;
	}

	for (;;)
	{
		dirent *Entry = readdir(Directory);
		if (!Entry)
		{
			break;
		}
		if (strcmp(Entry->d_name, ".") == 0 || strcmp(Entry->d_name, "..") == 0)
		{
			continue;
		}

		std::string FilePath = platform_join_path(Dir, Entry->d_name);
		struct stat Stat = {};
		if (stat(FilePath.c_str(), &Stat) != 0)
		{
			continue;
		}

		if (S_ISDIR(Stat.st_mode))
		{
			linux_font_scan_dir(FilePath, Depth + 1, Fonts);
			continue;
		}

		if (!linux_font_is_font_file(Entry->d_name))
		{
			continue;
		}

		std::string FileName = Entry->d_name;
		PlatformFontInfo Info = {};
		Info.Name = FileName.substr(0, FileName.size() - 4);
		Info.Path = FilePath;
		Fonts.push_back(Info);
	}

	closedir(Directory);
}

static int
linux_font_face_rank(const std::string &Path)
{
	size_t Slash = Path.find_last_of('/');
	std::string FileName = (Slash == std::string::npos) ? Path : Path.substr(Slash + 1);
	std::string Lower = linux_string_to_lower(FileName);
	if (Lower.find("regular") != std::string::npos)
	{
		return 0;
	}
	if (Lower.find("bold") != std::string::npos || Lower.find("italic") != std::string::npos ||
		Lower.find("oblique") != std::string::npos || Lower.find("light") != std::string::npos ||
		Lower.find("thin") != std::string::npos || Lower.find("black") != std::string::npos ||
		Lower.find("medium") != std::string::npos || Lower.find("condensed") != std::string::npos)
	{
		return 2;
	}
	return 1;
}

static bool
linux_font_less(const PlatformFontInfo &A, const PlatformFontInfo &B)
{
	std::string LowerA = linux_string_to_lower(A.Name);
	std::string LowerB = linux_string_to_lower(B.Name);
	if (LowerA != LowerB)
	{
		return LowerA < LowerB;
	}

	int RankA = linux_font_face_rank(A.Path);
	int RankB = linux_font_face_rank(B.Path);
	if (RankA != RankB)
	{
		return RankA < RankB;
	}

	return A.Path < B.Path;
}

static bool
linux_font_same(const PlatformFontInfo &A, const PlatformFontInfo &B)
{
	return A.Path == B.Path && linux_string_to_lower(A.Name) == linux_string_to_lower(B.Name);
}

// fontconfig enumeration. fontconfig is the display-server-agnostic font
// registry on Linux: the same query works under both X11 and Wayland, and it
// knows every font the desktop sees (distro font packages, NixOS profiles,
// ~/.local/share/fonts, flatpak extras, ...) with proper family names and the
// file backing each face. The library is dlopened (like libX11 above) so
// systems without it fall back to the plain directory scan below.
typedef int LinuxFcBool;
typedef int LinuxFcResult;
typedef struct LinuxFcConfig LinuxFcConfig;
typedef struct LinuxFcPattern LinuxFcPattern;
typedef struct LinuxFcObjectSet LinuxFcObjectSet;
typedef unsigned char LinuxFcChar8;

struct LinuxFcFontSet
{
	int NFont;
	int SFont;
	LinuxFcPattern **Fonts;
};

struct LinuxFcApiType
{
	void *Lib;
	bool Ready;

	LinuxFcBool (*FcInit)(void);
	LinuxFcPattern *(*FcPatternCreate)(void);
	void (*FcPatternDestroy)(LinuxFcPattern *);
	LinuxFcBool (*FcPatternAddBool)(LinuxFcPattern *, const char *, LinuxFcBool);
	LinuxFcObjectSet *(*FcObjectSetCreate)(void);
	LinuxFcBool (*FcObjectSetAdd)(LinuxFcObjectSet *, const char *);
	void (*FcObjectSetDestroy)(LinuxFcObjectSet *);
	LinuxFcFontSet *(*FcFontList)(LinuxFcConfig *, LinuxFcPattern *, LinuxFcObjectSet *);
	LinuxFcResult (*FcPatternGetString)(LinuxFcPattern *, const char *, int, LinuxFcChar8 **);
	void (*FcFontSetDestroy)(LinuxFcFontSet *);
};

static LinuxFcApiType g_LinuxFcApi = {};
static std::once_flag g_LinuxFcOnce;

static void
linux_fc_init_once()
{
	void *Lib = dlopen("libfontconfig.so.1", RTLD_LAZY | RTLD_LOCAL);
	if (!Lib)
	{
		return;
	}

	LinuxFcApiType Api = {};
	Api.Lib = Lib;
	Api.FcInit = reinterpret_cast<LinuxFcBool (*)(void)>(dlsym(Lib, "FcInit"));
	Api.FcPatternCreate = reinterpret_cast<LinuxFcPattern *(*)(void)>(dlsym(Lib, "FcPatternCreate"));
	Api.FcPatternDestroy = reinterpret_cast<void (*)(LinuxFcPattern *)>(dlsym(Lib, "FcPatternDestroy"));
	Api.FcPatternAddBool = reinterpret_cast<LinuxFcBool (*)(LinuxFcPattern *, const char *, LinuxFcBool)>(
		dlsym(Lib, "FcPatternAddBool"));
	Api.FcObjectSetCreate = reinterpret_cast<LinuxFcObjectSet *(*)(void)>(dlsym(Lib, "FcObjectSetCreate"));
	Api.FcObjectSetAdd = reinterpret_cast<LinuxFcBool (*)(LinuxFcObjectSet *, const char *)>(dlsym(Lib, "FcObjectSetAdd"));
	Api.FcObjectSetDestroy = reinterpret_cast<void (*)(LinuxFcObjectSet *)>(dlsym(Lib, "FcObjectSetDestroy"));
	Api.FcFontList = reinterpret_cast<LinuxFcFontSet *(*)(LinuxFcConfig *, LinuxFcPattern *, LinuxFcObjectSet *)>(
		dlsym(Lib, "FcFontList"));
	Api.FcPatternGetString = reinterpret_cast<LinuxFcResult (*)(LinuxFcPattern *, const char *, int, LinuxFcChar8 **)>(
		dlsym(Lib, "FcPatternGetString"));
	Api.FcFontSetDestroy = reinterpret_cast<void (*)(LinuxFcFontSet *)>(dlsym(Lib, "FcFontSetDestroy"));

	if (!Api.FcInit || !Api.FcPatternCreate || !Api.FcPatternDestroy || !Api.FcPatternAddBool ||
		!Api.FcObjectSetCreate || !Api.FcObjectSetAdd || !Api.FcObjectSetDestroy || !Api.FcFontList ||
		!Api.FcPatternGetString || !Api.FcFontSetDestroy)
	{
		dlclose(Lib);
		return;
	}

	if (!Api.FcInit())
	{
		dlclose(Lib);
		return;
	}

	Api.Ready = true;
	g_LinuxFcApi = Api;
}

static bool
linux_font_enumerate_fontconfig(std::vector<PlatformFontInfo> &Fonts)
{
	std::call_once(g_LinuxFcOnce, linux_fc_init_once);
	LinuxFcApiType *Api = &g_LinuxFcApi;
	if (!Api->Ready)
	{
		return false;
	}

	LinuxFcPattern *Pattern = Api->FcPatternCreate();
	LinuxFcObjectSet *ObjectSet = Api->FcObjectSetCreate();
	if (!Pattern || !ObjectSet)
	{
		if (Pattern)
		{
			Api->FcPatternDestroy(Pattern);
		}
		if (ObjectSet)
		{
			Api->FcObjectSetDestroy(ObjectSet);
		}
		return false;
	}

	Api->FcPatternAddBool(Pattern, "scalable", 1);
	Api->FcObjectSetAdd(ObjectSet, "family");
	Api->FcObjectSetAdd(ObjectSet, "file");

	LinuxFcFontSet *FontSet = Api->FcFontList(nullptr, Pattern, ObjectSet);
	Api->FcPatternDestroy(Pattern);
	Api->FcObjectSetDestroy(ObjectSet);
	if (!FontSet)
	{
		return false;
	}

	for (int Index = 0; Index < FontSet->NFont; Index++)
	{
		LinuxFcChar8 *Family = nullptr;
		LinuxFcChar8 *File = nullptr;
		if (Api->FcPatternGetString(FontSet->Fonts[Index], "family", 0, &Family) != 0)
		{
			continue;
		}
		if (Api->FcPatternGetString(FontSet->Fonts[Index], "file", 0, &File) != 0)
		{
			continue;
		}
		if (!Family || !File || !Family[0] || !File[0])
		{
			continue;
		}

		PlatformFontInfo Info = {};
		Info.Name = reinterpret_cast<const char *>(Family);
		Info.Path = reinterpret_cast<const char *>(File);
		Fonts.push_back(Info);
	}

	Api->FcFontSetDestroy(FontSet);

	return Fonts.size() > 0;
}

inline std::vector<PlatformFontInfo>
platform_enumerate_fonts()
{
	std::vector<PlatformFontInfo> Fonts;
	if (linux_font_enumerate_fontconfig(Fonts))
	{
		std::sort(Fonts.begin(), Fonts.end(), linux_font_less);
		Fonts.erase(std::unique(Fonts.begin(), Fonts.end(), linux_font_same), Fonts.end());
		return Fonts;
	}

	std::vector<std::string> CandidateDirs;

	const char *Home = getenv("HOME");

	const char *XdgDataHome = getenv("XDG_DATA_HOME");
	if (XdgDataHome && XdgDataHome[0] != '\0')
	{
		linux_font_push_dir(CandidateDirs, platform_join_path(XdgDataHome, "fonts"));
	}
	else if (Home && Home[0] != '\0')
	{
		linux_font_push_dir(CandidateDirs, platform_join_path(Home, ".local/share/fonts"));
	}

	if (Home && Home[0] != '\0')
	{
		linux_font_push_dir(CandidateDirs, platform_join_path(Home, ".fonts"));
	}

	std::string DataDirs = "/usr/local/share:/usr/share:/run/current-system/sw/share";
	const char *XdgDataDirs = getenv("XDG_DATA_DIRS");
	if (XdgDataDirs && XdgDataDirs[0] != '\0')
	{
		DataDirs = XdgDataDirs;
	}

	size_t Pos = 0;
	for (;;)
	{
		size_t Next = DataDirs.find(':', Pos);
		std::string Entry = (Next == std::string::npos) ? DataDirs.substr(Pos) : DataDirs.substr(Pos, Next - Pos);
		if (!Entry.empty())
		{
			linux_font_push_dir(CandidateDirs, platform_join_path(Entry, "fonts"));
		}
		if (Next == std::string::npos)
		{
			break;
		}
		Pos = Next + 1;
	}

	linux_font_push_dir(CandidateDirs, "/usr/share/fonts");
	linux_font_push_dir(CandidateDirs, "/usr/local/share/fonts");

	for (const std::string &Dir : CandidateDirs)
	{
		linux_font_scan_dir(Dir, 0, Fonts);
	}

	std::sort(Fonts.begin(), Fonts.end(), linux_font_less);

	return Fonts;
}

#ifdef VOICETYPER_HAVE_X11
#undef Bool
#undef None
#undef Status
#undef Success
#undef True
#undef False
#endif
