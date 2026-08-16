#pragma once

#include "runtime_types.h"
#include "whisper.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

inline bool
vad_model_file_available(const char *VadModelPath)
{
	if (VadModelPath == nullptr || VadModelPath[0] == '\0') return false;
	std::ifstream F(VadModelPath, std::ios::binary);
	return F.good();
}

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

	if (EnableVad && !vad_model_file_available(VadModelPath))
	{
		printf("[transcription] VAD model not found at '%s'; falling back to non-VAD inference\n",
			VadModelPath ? VadModelPath : "(null)");
		EnableVad = false;
	}

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
	std::string *OutText,
	std::vector<TranscribedWord> *OutWords = nullptr)
{
	OutText->clear();
	if (OutWords) OutWords->clear();

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

	if (OutWords)
	{
		for (int i = 0; i < NumSegments; i++)
		{
			int NumTokens = whisper_full_n_tokens(Context, i);
			for (int j = 0; j < NumTokens; j++)
			{
				const char *TokenText = whisper_full_get_token_text(Context, i, j);
				if (!TokenText || TokenText[0] == '\0') continue;
				if (strncmp(TokenText, "[_TT_", 5) == 0) continue;

				float P = whisper_full_get_token_p(Context, i, j);

				if (TokenText[0] == ' ' || OutWords->empty())
				{
					TranscribedWord Word;
					Word.Text = TokenText;
					Word.Confidence = P;
					OutWords->push_back(Word);
				}
				else
				{
					TranscribedWord &Word = OutWords->back();
					Word.Text += TokenText;
					if (P < Word.Confidence) Word.Confidence = P;
				}
			}
		}
	}

	return 0;
}
