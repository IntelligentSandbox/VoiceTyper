#pragma once

#include <cmath>
#include <vector>

// Minimum RMS energy to bother sending a chunk to whisper.
#define PIPELINE_SILENCE_RMS_THRESHOLD 0.002f

// How often (ms) the stream segmenter thread polls the audio buffer for energy levels.
#define STREAM_POLL_INTERVAL_MS 100

// RMS energy threshold for classifying a poll interval as speech vs silence.
#define STREAM_SPEECH_RMS_THRESHOLD 0.002f

// Minimum chunk duration (ms) before a speech->silence transition can trigger a cutoff.
#define STREAM_MIN_CHUNK_DURATION_MS 1000

// How long silence (ms) must persist after speech before cutting the chunk.
#define STREAM_SILENCE_DURATION_MS 500

// Offline replica of stream_segment_thread's chunking logic. Operates on a complete
// PCM buffer and returns the silence-bounded chunks the live segmenter would have
// produced, including the final flush on stop (equivalent to StreamingFinalizeOnStop).
static std::vector<std::vector<float>>
chunk_audio_for_streaming(const std::vector<float> &Samples, int SampleRate)
{
	auto compute_rms_block = [](const float *S, int N) -> float {
		if (N <= 0) return 0.0f;
		double Sum = 0.0;
		for (int i = 0; i < N; i++)
		{
			Sum += (double)S[i] * (double)S[i];
		}
		return (float)std::sqrt(Sum / (double)N);
	};

	std::vector<std::vector<float>> Chunks;
	int Total = (int)Samples.size();
	if (Total <= 0) return Chunks;

	int PollSamples = SampleRate * STREAM_POLL_INTERVAL_MS / 1000;
	if (PollSamples <= 0) PollSamples = 1;

	int SilenceMs = 0;
	bool HasSpeech = false;
	std::vector<float> Accum;
	Accum.reserve(Total);

	int Pos = 0;
	while (Pos < Total)
	{
		int Remaining = Total - Pos;
		int RecentCount = PollSamples < Remaining ? PollSamples : Remaining;
		const float *BlockStart = Samples.data() + Pos;
		float CurrentRms = compute_rms_block(BlockStart, RecentCount);
		Pos += RecentCount;

		Accum.insert(Accum.end(), BlockStart, BlockStart + RecentCount);

		if (CurrentRms >= STREAM_SPEECH_RMS_THRESHOLD)
		{
			HasSpeech = true;
			SilenceMs = 0;
		}
		else if (HasSpeech)
		{
			SilenceMs += STREAM_POLL_INTERVAL_MS;
		}
		else
		{
			Accum.clear();
			continue;
		}

		int BufferDurationMs = (int)Accum.size() * 1000 / SampleRate;
		if (HasSpeech &&
			SilenceMs >= STREAM_SILENCE_DURATION_MS &&
			BufferDurationMs >= STREAM_MIN_CHUNK_DURATION_MS)
		{
			Chunks.push_back(std::move(Accum));
			Accum.clear();
			Accum.reserve(Total - Pos);
			SilenceMs = 0;
			HasSpeech = false;
		}
	}

	int BufferDurationMs = (int)Accum.size() * 1000 / SampleRate;
	if (HasSpeech && BufferDurationMs >= STREAM_MIN_CHUNK_DURATION_MS)
	{
		Chunks.push_back(std::move(Accum));
	}

	return Chunks;
}
