#pragma once

#include "platform.h"

inline void play_start_recording_sound()  { platform_play_sound(1000, 200); }
inline void play_stop_recording_sound()   { platform_play_sound(800, 200); }
inline void play_cancel_recording_sound() { platform_play_sound(400, 300); }
