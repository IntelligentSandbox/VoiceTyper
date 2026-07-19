#pragma once

#include "whisper.h"

#include <string>

inline whisper_full_params
make_transcription_whisper_params(int ThreadCount, bool EnableVad, const char *VadModelPath)
{
	whisper_full_params Params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
	Params.language         = "en";
	Params.translate        = false;
	Params.no_context       = true;
	Params.print_progress   = false;
	Params.print_realtime   = false;
	Params.print_special    = false;
	Params.print_timestamps = false;
	Params.n_threads        = ThreadCount;
	Params.vad              = EnableVad;

	if (EnableVad)
	{
		Params.vad_model_path = VadModelPath;

		whisper_vad_params VadParams      = whisper_vad_default_params();
		VadParams.threshold               = 0.5f;
		VadParams.min_speech_duration_ms  = 250;
		VadParams.min_silence_duration_ms = 500;
		Params.vad_params                 = VadParams;
	}

	return Params;
}

inline int
transcribe_pcm_to_string(
	whisper_context *Context,
	whisper_full_params &Params,
	const float *Samples,
	int SampleCount,
	std::string *OutText)
{
	OutText->clear();

	int Ret = whisper_full(Context, Params, Samples, SampleCount);
	if (Ret != 0) return Ret;

	int NumSegments = whisper_full_n_segments(Context);
	for (int i = 0; i < NumSegments; i++)
	{
		const char *Text = whisper_full_get_segment_text(Context, i);
		if (Text && Text[0] != '\0') *OutText += Text;
	}

	size_t Start = OutText->find_first_not_of(" \t\n\r");
	if (Start == std::string::npos)
	{
		OutText->clear();
	}
	else
	{
		size_t End = OutText->find_last_not_of(" \t\n\r");
		*OutText = OutText->substr(Start, End - Start + 1);
	}

	return 0;
}
