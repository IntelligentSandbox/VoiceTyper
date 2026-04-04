#pragma once

#include "platform.h"
#include "state.h"

inline void play_start_recording_sound(const SoundConfig *Cfg)
{
	platform_play_sound(Cfg->FreqHz, SOUND_START_DURATION_MS, Cfg->Volume);
}

inline void play_stop_recording_sound(const SoundConfig *Cfg)
{
	platform_play_sound(Cfg->FreqHz, SOUND_STOP_DURATION_MS, Cfg->Volume);
}

inline void play_cancel_recording_sound(const SoundConfig *Cfg)
{
	platform_play_sound(Cfg->FreqHz, SOUND_CANCEL_DURATION_MS, Cfg->Volume);
}
