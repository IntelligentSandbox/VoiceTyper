#pragma once

#include "imgui.h"
#include "runtime_types.h"

#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Build-time constants
//
// Single source of truth for defaults and other values that never change at
// runtime: numbers, strings, bools and constant structs. The only value
// injected from the outside is the version string, which CMake computes from
// the VERSION file and git; its #ifndef fallback is used when building without
// CMake.
// ---------------------------------------------------------------------------

// Full version string (set by CMake)
#ifndef VOICETYPER_VERSION_FULL
#define VOICETYPER_VERSION_FULL "0.0.0-unknown"
#endif
inline constexpr const char *VERSION_FULL = VOICETYPER_VERSION_FULL;

// Fixed app-state update rate in Hz
inline constexpr int APP_UPDATE_HZ = 100;
static_assert(APP_UPDATE_HZ > 0, "APP_UPDATE_HZ must be positive");

// Maximum fixed app ticks to process per loop
inline constexpr int APP_UPDATE_MAX_CATCH_UP_TICKS = 5;
static_assert(APP_UPDATE_MAX_CATCH_UP_TICKS > 0, "APP_UPDATE_MAX_CATCH_UP_TICKS must be positive");

// Minimum main window width in px for the two-column layout; below it the
// columns stack vertically
inline constexpr int TWO_COLUMN_MIN_WIDTH = 1000;
static_assert(TWO_COLUMN_MIN_WIDTH > 0, "TWO_COLUMN_MIN_WIDTH must be positive");

// Use Whisper VAD for silence-bounded streaming/record chunks
inline constexpr bool STREAMING_WHISPER_VAD = true;
inline constexpr bool RECORD_WHISPER_VAD = true;

// ---------------------------------------------------------------------------
// Window
// ---------------------------------------------------------------------------
inline constexpr int WINDOW_DEFAULT_WIDTH  = 700;
inline constexpr int WINDOW_DEFAULT_HEIGHT = 575;
inline constexpr int WINDOW_MIN_WIDTH      = 320;
inline constexpr int WINDOW_MIN_HEIGHT     = 240;
inline constexpr int RENDER_REFRESH_FALLBACK_HZ = 60;
inline constexpr int RENDER_SLEEP_MAX_MS         = 16;
inline constexpr int RENDER_IDLE_REFRESH_HZ      = 10;
inline constexpr int RENDER_IDLE_DELAY_MS        = 1000;
inline constexpr const char *APP_ICON_PATH = "media/voicetyper-icon.png";

// ---------------------------------------------------------------------------
// Audio capture
// ---------------------------------------------------------------------------
inline constexpr int MAX_AUDIO_DEVICE_NAME_LENGTH  = 512;
inline constexpr int AUDIO_CAPTURE_SAMPLE_RATE     = 16000;
inline constexpr int AUDIO_CAPTURE_CHANNELS        = 1;
inline constexpr int AUDIO_CAPTURE_BITS_PER_SAMPLE = 16;
inline constexpr int AUDIO_CAPTURE_BUFFER_MS       = 20;
inline constexpr int AUDIO_CAPTURE_BUFFER_COUNT    = 16;

// Audio input memory pool. Capture audio is accumulated in fixed-size pooled
// blocks (a 1-minute block = 960,000 floats = ~3.8MB) chained per clip, so
// steady-state recording/streaming performs no heap allocations and captured
// audio is never realloc-copied.
inline constexpr int AUDIO_POOL_BLOCK_MS            = 60000;
inline constexpr int AUDIO_POOL_INITIAL_BLOCKS      = 3;
inline constexpr int AUDIO_POOL_REFILL_BATCH_BLOCKS = 3;
// Refill trigger: when the free-block count falls to this low-water mark,
// pool_acquire allocates another refill batch.
inline constexpr int AUDIO_POOL_REFILL_LOW_WATER    = 0;
// Contiguous whisper-input staging buffer seed size, reserved once at startup
// (30s of audio). Grows geometrically afterwards, never shrinks.
inline constexpr int AUDIO_STAGING_INITIAL_MS       = 30000;
inline constexpr int AUDIO_POOL_BLOCK_SAMPLES =
	AUDIO_CAPTURE_SAMPLE_RATE * AUDIO_POOL_BLOCK_MS / 1000;
static_assert(AUDIO_POOL_BLOCK_MS > 0, "AUDIO_POOL_BLOCK_MS must be positive");
static_assert(AUDIO_POOL_INITIAL_BLOCKS > 0, "AUDIO_POOL_INITIAL_BLOCKS must be positive");
static_assert(AUDIO_POOL_REFILL_BATCH_BLOCKS > 0, "AUDIO_POOL_REFILL_BATCH_BLOCKS must be positive");
static_assert(AUDIO_POOL_REFILL_LOW_WATER >= 0, "AUDIO_POOL_REFILL_LOW_WATER must be non-negative");
static_assert(AUDIO_POOL_BLOCK_SAMPLES > 0, "AUDIO_POOL_BLOCK_SAMPLES must be positive");

// ---------------------------------------------------------------------------
// Sound
// ---------------------------------------------------------------------------
inline constexpr int SOUND_DEFAULT_START_FREQ  = 880;
inline constexpr int SOUND_DEFAULT_STOP_FREQ   = 659;
inline constexpr int SOUND_DEFAULT_CANCEL_FREQ = 330;
inline constexpr int SOUND_DEFAULT_VOLUME      = 50;
inline constexpr int SOUND_MIN_FREQ            = 200;
inline constexpr int SOUND_MAX_FREQ            = 2000;
inline constexpr int SOUND_START_DURATION_MS   = 200;
inline constexpr int SOUND_STOP_DURATION_MS    = 200;
inline constexpr int SOUND_CANCEL_DURATION_MS  = 300;
inline constexpr int SOUND_PREVIEW_DURATION_MS = 120;

// ---------------------------------------------------------------------------
// Colors
// ---------------------------------------------------------------------------
inline constexpr ImVec4 BUTTON_COLOR_GREEN = ImVec4(0.0f, 0.50f, 0.0f, 1.0f);
inline constexpr ImVec4 BUTTON_COLOR_RED   = ImVec4(0.75f, 0.07f, 0.13f, 1.0f);
inline constexpr ImVec4 BUTTON_COLOR_GREY  = ImVec4(0.50f, 0.50f, 0.50f, 1.0f);
inline constexpr ImVec4 BUTTON_COLOR_BLUE  = ImVec4(0.13f, 0.59f, 0.95f, 1.0f);

inline constexpr ColorRgba TOAST_COLOR_ERROR   = ColorRgba{0.70f, 0.10f, 0.10f, 1.0f};
inline constexpr ColorRgba TOAST_COLOR_SUCCESS = ColorRgba{0.10f, 0.55f, 0.20f, 1.0f};

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------
inline constexpr int FONT_SUGGESTION_MAX_ROWS = 5;
inline constexpr double TOAST_DURATION_SECONDS = 2.0;
inline constexpr int RECORD_INDICATOR_DEFAULT_DELAY_MS = 50;

// ---------------------------------------------------------------------------
// Streaming segmenter
// ---------------------------------------------------------------------------

// Minimum RMS energy to bother sending a chunk to whisper.
inline constexpr float PIPELINE_SILENCE_RMS_THRESHOLD = 0.0005f;

// How often (ms) the stream segmenter thread polls the audio buffer for energy levels.
inline constexpr int STREAM_POLL_INTERVAL_MS = 100;

// Cold-start RMS energy threshold for classifying a poll interval as speech vs
// silence. Once capture starts the threshold adapts to the measured noise floor.
inline constexpr float STREAM_SPEECH_RMS_THRESHOLD = 0.002f;

// The speech threshold tracks the measured noise floor: threshold = floor * RATIO,
// clamped to [MIN, MAX] so digital silence cannot drive it to zero and loud rooms
// cannot push it past reasonable speech levels.
inline constexpr float STREAM_SPEECH_RMS_RATIO = 2.5f;
inline constexpr float STREAM_SPEECH_RMS_MIN   = 0.0015f;
inline constexpr float STREAM_SPEECH_RMS_MAX   = 0.02f;

// Noise floor tracking rates, applied per poll while no speech is in progress:
// rises quickly to follow noise ramps, decays slowly so brief quiet gaps do not
// collapse it under the next utterance.
inline constexpr float STREAM_NOISE_FLOOR_RISE  = 0.3f;
inline constexpr float STREAM_NOISE_FLOOR_DECAY = 0.05f;

// Minimum chunk duration (ms) before a speech->silence transition can trigger a cutoff.
inline constexpr int STREAM_MIN_CHUNK_DURATION_MS = 1000;

// How long silence (ms) must persist after speech before cutting the chunk.
inline constexpr int STREAM_SILENCE_DURATION_MS = 500;

// ---------------------------------------------------------------------------
// Updater
// ---------------------------------------------------------------------------
inline constexpr const char *UPDATER_GITHUB_REPO = "IntelligentSandbox/VoiceTyper";
inline constexpr const char *UPDATER_API_RELEASES_URL =
	"https://api.github.com/repos/IntelligentSandbox/VoiceTyper/releases?per_page=100";
inline constexpr const char *UPDATER_RELEASES_URL =
	"https://github.com/IntelligentSandbox/VoiceTyper/releases/latest";

// ---------------------------------------------------------------------------
// Models
// ---------------------------------------------------------------------------
inline constexpr const char *WHISPER_HF_BASE_URL =
	"https://huggingface.co/ggerganov/whisper.cpp/resolve/main";
inline constexpr const char *DISTIL_WHISPER_HF_BASE_URL =
	"https://huggingface.co/distil-whisper/distil-large-v3.5-ggml/resolve/main";
inline constexpr const char *VAD_HF_BASE_URL =
	"https://huggingface.co/ggml-org/whisper-vad/resolve/main";
inline constexpr const char *VAD_MODEL_FILENAME     = "ggml-silero-v5.1.2.bin";
inline constexpr const char *VAD_MODEL_DISPLAY_NAME = "Silero VAD v5.1.2";
inline constexpr int64_t VAD_MODEL_SIZE_BYTES       = 885LL * 1024;

struct CatalogModel
{
	const char *Name;
	const char *Description;
	int64_t     SizeBytes;
	bool        IsEnglishOnly;
};

inline const std::vector<CatalogModel> MODEL_CATALOG = {
	{"tiny.en",        "Tiny English-only",        75ULL   * 1024 * 1024, true},
	{"tiny",           "Tiny multilingual",        75ULL   * 1024 * 1024, false},
	{"base.en",        "Base English-only",        148ULL  * 1024 * 1024, true},
	{"base",           "Base multilingual",        148ULL  * 1024 * 1024, false},
	{"small.en",       "Small English-only",       466ULL  * 1024 * 1024, true},
	{"small",          "Small multilingual",       466ULL  * 1024 * 1024, false},
	{"medium.en",      "Medium English-only",      1530ULL * 1024 * 1024, true},
	{"medium",         "Medium multilingual",      1530ULL * 1024 * 1024, false},
	{"large-v1",       "Large v1 multilingual",    3070ULL * 1024 * 1024, false},
	{"large-v2",       "Large v2 multilingual",    3070ULL * 1024 * 1024, false},
	{"large-v3",       "Large v3 multilingual",    3070ULL * 1024 * 1024, false},
	{"large-v3-turbo", "Large v3 Turbo",           1620ULL * 1024 * 1024, false},
	{"large-v3-turbo-q5_0", "Large v3 Turbo (q5_0 quant)", 548ULL  * 1024 * 1024, false},
	{"distil-large-v3.5",   "Distil Large v3.5 English",   1450ULL * 1024 * 1024, true},
};

// ---------------------------------------------------------------------------
// Settings store / diagnostics
// ---------------------------------------------------------------------------

// Per-program paste hotkey overrides live as paste_hotkey_app_<process>_modifiers/_key
// entries in settings.ini.
inline constexpr const char *PASTE_HOTKEY_OVERRIDE_SETTING_PREFIX = "paste_hotkey_app_";

inline constexpr const char *CRASH_DUMP_PREFIX = "voicetyper-crash-";
inline constexpr const char *CRASH_DUMP_SUFFIX = ".dmp";
inline constexpr const char *CRASH_SEEN_SUFFIX = ".seen";
