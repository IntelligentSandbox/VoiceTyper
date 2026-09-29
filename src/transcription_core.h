#pragma once

#include "runtime_types.h"
#include "whisper.h"
#include "parakeet.h"

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

// whisper occasionally decodes a trailing near-empty audio window into a
// "[BLANK_AUDIO]" vocab token (a special non-speech marker). It is never real
// transcription output, so segments consisting solely of it are dropped.
inline bool
is_blank_audio_segment_text(const char *Text)
{
	while (*Text == ' ') Text++;

	const char *Marker = "[BLANK_AUDIO]";
	while (*Marker && *Text == *Marker)
	{
		Text++;
		Marker++;
	}

	while (*Text == ' ') Text++;

	return *Marker == '\0' && *Text == '\0';
}

inline whisper_full_params
make_transcription_whisper_params(int ThreadCount, bool EnableVad, const char *VadModelPath,
	const char *InitialPrompt = nullptr)
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

	// With no_context = true, whisper_full clears the prompt history at the
	// start of every call and re-seeds it from initial_prompt, so the hint is
	// applied to each record take / streaming chunk independently.
	if (InitialPrompt && InitialPrompt[0] != '\0')
	{
		Params.initial_prompt = InitialPrompt;
	}

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

inline parakeet_full_params
make_transcription_parakeet_params(int ThreadCount)
{
	parakeet_full_params Params = parakeet_full_default_params(PARAKEET_SAMPLING_GREEDY);
	Params.n_threads = ThreadCount;

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

	// Cap the encoder context to the actual audio length. The conv/encoder
	// graphs are rebuilt per whisper_full call and size off this value, so
	// short utterances skip the worst-case 30s worth of encoder compute while
	// the init-time compute buffers stay valid (they only shrink their use).
	{
		int AudioFrames    = (SampleCount + 159) / 160;
		int NeededAudioCtx = (AudioFrames + 1) / 2 + 8;
		if (NeededAudioCtx < 64) NeededAudioCtx = 64;
		int ModelMaxAudioCtx = whisper_n_audio_ctx(Context);
		if (NeededAudioCtx < ModelMaxAudioCtx) Params.audio_ctx = NeededAudioCtx;
		else Params.audio_ctx = ModelMaxAudioCtx;
	}

	int Ret = whisper_full(Context, Params, Samples, SampleCount);
	if (Ret != 0) return Ret;

	int NumSegments = whisper_full_n_segments(Context);
	for (int i = 0; i < NumSegments; i++)
	{
		const char *Text = whisper_full_get_segment_text(Context, i);
		if (!Text || Text[0] == '\0') continue;
		if (is_blank_audio_segment_text(Text)) continue;
		*OutText += Text;
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
			if (whisper_full_get_token_id(Context, i, j) >= whisper_token_eot(Context)) continue;
			if (is_blank_audio_segment_text(TokenText)) continue;

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

// parakeet_full_params.audio_ctx is left at its default 0: parakeet_chunk
// then caps the encoder graph to min(mel_len, model max) internally, sized
// exactly to the actual audio, and longer audio takes a dynamic graph path
// that ignores audio_ctx; the whisper-side cap formula has no equivalent.
inline std::string
parakeet_piece_to_word_text(const char *Piece)
{
	std::string Text;
	size_t Len = std::strlen(Piece);
	Text.reserve(Len);
	for (size_t i = 0; i < Len; i++)
	{
		if (i + 3 <= Len && std::strncmp(Piece + i, "\xE2\x96\x81", 3) == 0)
		{
			Text += ' ';
			i += 2;
			continue;
		}
		Text += Piece[i];
	}

	return Text;
}

inline int
transcribe_pcm_to_string(
	parakeet_context *Context,
	parakeet_full_params &Params,
	const float *Samples,
	int SampleCount,
	std::string *OutText,
	std::vector<TranscribedWord> *OutWords = nullptr)
{
	OutText->clear();
	if (OutWords) OutWords->clear();

	int Ret = parakeet_full(Context, Params, Samples, SampleCount);
	if (Ret != 0) return Ret;

	int NumSegments = parakeet_full_n_segments(Context);
	for (int i = 0; i < NumSegments; i++)
	{
		const char *Text = parakeet_full_get_segment_text(Context, i);
		if (!Text || Text[0] == '\0') continue;
		if (is_blank_audio_segment_text(Text)) continue;
		*OutText += Text;
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
			int NumTokens = parakeet_full_n_tokens(Context, i);
			for (int j = 0; j < NumTokens; j++)
			{
				const char *TokenText = parakeet_full_get_token_text(Context, i, j);
				if (!TokenText || TokenText[0] == '\0') continue;
				if (parakeet_full_get_token_id(Context, i, j) >= parakeet_token_blank(Context)) continue;
				if (is_blank_audio_segment_text(TokenText)) continue;

				std::string PieceText = parakeet_piece_to_word_text(TokenText);
				if (PieceText.empty()) continue;

				// SentencePiece pieces mark word starts with the meta-space
				// character U+2581 (UTF-8: E2 96 81); '_' is checked as a
				// fallback, matching is_word_start_token in parakeet.cpp.
				bool IsWordStart = std::strncmp(TokenText, "\xE2\x96\x81", 3) == 0 || TokenText[0] == '_';

				float P = parakeet_full_get_token_p(Context, i, j);

				if (IsWordStart || OutWords->empty())
				{
					TranscribedWord Word;
					Word.Text = PieceText;
					Word.Confidence = P;
					OutWords->push_back(Word);
				}
				else
				{
					TranscribedWord &Word = OutWords->back();
					Word.Text += PieceText;
					if (P < Word.Confidence) Word.Confidence = P;
				}
			}
		}
	}

	return 0;
}
