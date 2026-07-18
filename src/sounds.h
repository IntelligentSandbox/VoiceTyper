#pragma once

#include "host_services.h"
#include "state.h"

inline void play_start_recording_sound(PlatformRuntimeState *Platform, int FreqHz)
{
	platform_play_sound(Platform, FreqHz, SOUND_START_DURATION_MS);
}

inline void play_stop_recording_sound(PlatformRuntimeState *Platform, int FreqHz)
{
	platform_play_sound(Platform, FreqHz, SOUND_STOP_DURATION_MS);
}

inline void play_cancel_recording_sound(PlatformRuntimeState *Platform, int FreqHz)
{
	platform_play_sound(Platform, FreqHz, SOUND_CANCEL_DURATION_MS);
}
