# Streaming VAD Benchmark Plan

## Goal
Evaluate whether streaming chunks that are already silence-bounded by the app's first-pass
energy VAD can skip the second-pass Whisper Silero VAD without hurting transcription quality.

The benchmark compares three configurations against fixed audio inputs:

| Variant | Mode | Whisper VAD | Purpose |
| --- | --- | --- | --- |
| `record-vad-on` | record (whole audio) | on | Control: quality reference with `VOICETYPER_RECORD_WHISPER_VAD` semantics |
| `streaming-vad-on` | streaming (chunked) | on | Current default: `VOICETYPER_STREAMING_WHISPER_VAD=ON` |
| `streaming-vad-off` | streaming (chunked) | off | Proposed: `VOICETYPER_STREAMING_WHISPER_VAD=OFF` |

The record variant transcribes the entire WAV in one `whisper_full` call with VAD enabled,
matching the existing record-mode path. The two streaming variants split the same WAV into
silence-bounded chunks using the same segmenter logic as the live app, then transcribe each
chunk with `single_segment = true`, toggling only `Params.vad`.

## Background
`VOICETYPER_STREAMING_WHISPER_VAD` and `VOICETYPER_RECORD_WHISPER_VAD` are compile-time CMake
options wired into `src/audio_pipeline.h` at the `make_transcription_whisper_params` call sites.
They are not runtime environment variables. Rather than rebuild per variant, the benchmark
exercises the same `Params.vad` toggle through a runtime `--vad on|off` CLI flag, which is
functionally identical to flipping the macro. The compile-time flags remain in place for the app.

## Scope
- Extend the existing `VoiceTyperBench` executable with `--mode` and `--vad` flags.
- Add an offline streaming chunker that mirrors `stream_segment_thread` using the shared
  constants in `src/stream_chunker.h`.
- Add a runner script that builds once and runs all three variants.
- Record per-chunk latency, total streaming latency, and output text quality.
- Keep normal app behavior unchanged.

## Shared Chunker
`src/stream_chunker.h` owns the streaming constants (`PIPELINE_SILENCE_RMS_THRESHOLD`,
`STREAM_POLL_INTERVAL_MS`, `STREAM_SPEECH_RMS_THRESHOLD`, `STREAM_MIN_CHUNK_DURATION_MS`,
`STREAM_SILENCE_DURATION_MS`) and an offline `chunk_audio_for_streaming` function. The app's
`audio_pipeline.h` includes this header for the constants; the benchmark includes it for the
offline chunker. This keeps the benchmark's chunking identical to the live segmenter without
duplicating tuning values.

The offline chunker replicates the live segmenter's per-poll RMS classification, leading-silence
discard, min-duration gate, and silence-duration cutoff. It performs a final flush equivalent to
`StreamingFinalizeOnStop = true` so the trailing speech segment is not lost.

## Benchmark CLI
The benchmark executable is the existing `VoiceTyperBench`, extended with three new flags.
Default behavior (`--mode record --vad off`) is unchanged, so the release-optimization runner
continues to work without modification.

```bash
VoiceTyperBench.exe \
	--mode streaming \
	--vad on \
	--vad-model vad_models/ggml-silero-v5.1.2.bin \
	--model stt_models/ggml-base.en.bin \
	--audio bench/jfk.wav \
	--expected-text "..." \
	--warmup 1 \
	--iterations 5 \
	--threads 8
```

Supported arguments (additions in bold):
- `--model <path>`: Whisper model path.
- `--audio <path>`: required WAV input path.
- `--expected-text <text>`: optional expected transcription.
- **`--mode <record|streaming>`**: default `record`.
- **`--vad <on|off>`**: default `off`.
- **`--vad-model <path>`**: default `vad_models/ggml-silero-v5.1.2.bin`, used when `--vad on`.
- `--warmup <count>`: default `1`.
- `--iterations <count>`: default `5`.
- `--threads <count>`: default to hardware concurrency.
- `--cuda-device <index>`: default `0`, used by CUDA builds.

## Runtime Metrics
The benchmark prints one JSON object to stdout.

Record mode (`unit_count` is 1, `per_unit_ms` inner arrays have one entry each):

```json
{"mode":"record","vad":true,"model_load_ms":820,"unit_count":1,"unit_durations_ms":[11000],"transcribe_ms":[410,399,402,405,401],"per_unit_ms":[[410],[399],[402],[405],[401]],"text":"...","expected_text":"...","expected_text_match":true}
```

Streaming mode (`unit_count` is the number of silence-bounded chunks):

```json
{"mode":"streaming","vad":false,"model_load_ms":820,"unit_count":3,"unit_durations_ms":[3200,1500,2100],"transcribe_ms":[420,415,418,422,416],"per_unit_ms":[[120,200,90],[118,198,88],[122,202,86],[120,200,90],[119,198,87]],"text":"...","expected_text":"...","expected_text_match":true}
```

Fields:
- `mode`: `record` or `streaming`.
- `vad`: whether Whisper VAD was enabled.
- `model_load_ms`: model load wall time.
- `unit_count`: number of transcription units (1 for record, chunk count for streaming).
- `unit_durations_ms`: audio duration in ms of each unit.
- `transcribe_ms`: per-iteration total transcription wall time (sum of all units).
- `per_unit_ms`: per-iteration, per-unit transcription wall time.
- `text`: final transcription (chunk texts joined by a single space for streaming).
- `expected_text` / `expected_text_match`: present when `--expected-text` is provided.

The key streaming latency metric is `per_unit_ms`, since each chunk is transcribed as it arrives
and the user sees text after each chunk. `transcribe_ms` is the per-iteration total, comparable
to the record variant.

## Text Quality Comparison
When `--expected-text` is provided, the benchmark normalizes line endings and trims outer
whitespace, then compares exactly. For streaming, chunk texts are joined by a single space
before normalization. Exact match may fail due to chunk-boundary word splits or trailing-silence
effects; the `text` field is always emitted for manual inspection.

## Runner Script
`tools/benchmark_streaming_vad.sh` builds the benchmark once and runs all three variants.

Runner defaults:
- CPU build.
- `--model stt_models/ggml-base.en.bin`.
- `--vad-model vad_models/ggml-silero-v5.1.2.bin`.
- `--warmup 1`.
- `--iterations 5`.
- `--threads <hardware concurrency>`.
- `--output build/bench/streaming_vad_results.jsonl`.
- Overwrite by default; `--append` to accumulate across test cases.
- `--label` defaults to the audio basename without extension.

Runner options:
- `--audio <path>`: required.
- `--model <path>`, `--vad-model <path>`, `--expected-text <text>`, `--label <name>`.
- `--cuda`: build and run with CUDA.
- `--cuda-device <index>`, `--warmup <count>`, `--iterations <count>`, `--threads <count>`.
- `--output <path>`, `--append`.

The runner writes one JSONL row per variant, enriched with `label`, `variant`, `backend`, and
the benchmark runtime body. Build failures write a failure row with the phase and an error
summary.

## Test Matrix
The evaluation should cover these cases with user-supplied 16 kHz mono WAV fixtures.
Use `--append` with distinct `--label` values to accumulate all cases into one JSONL file.

| Label | Description | What to watch |
| --- | --- | --- |
| `clean-speech` | Isolated utterance with clear silence gaps | Baseline quality and latency |
| `quiet-speech` | Low-volume speech near the RMS threshold | Whether VAD-off drops quiet chunks |
| `noisy-background` | Continuous background noise above threshold | Whether VAD-on trims noise that VAD-off includes |
| `short-utterance` | Utterance shorter than `STREAM_MIN_CHUNK_DURATION_MS` | Whether the chunker drops short speech |
| `clipped-first-word` | Speech starting at audio start | Whether leading-word is lost |
| `clipped-last-word` | Speech ending at audio end | Whether trailing-word is lost in final flush |

Each case is run with `--expected-text` when a ground-truth transcript is available.

## Implementation Order
1. Extract shared streaming constants and offline chunker into `src/stream_chunker.h`.
2. Update `src/audio_pipeline.h` to include the shared header.
3. Extend `src/benchmark_main.cpp` with `--mode`, `--vad`, `--vad-model` and the streaming path.
4. Add `tools/benchmark_streaming_vad.sh`.
5. Verify CPU Release build.
6. Verify CUDA Release build when CUDA is available.
7. Run at least one CPU benchmark against `bench/jfk.wav` and confirm JSON output.

## Out Of Scope
- Committed Whisper model files.
- Additional committed audio fixtures beyond `bench/jfk.wav`.
- Audio resampling and stereo-to-mono conversion.
- GUI automation.
- Statistical analysis beyond recording per-iteration and per-unit timings.
- Changing the app's compile-time VAD defaults; this only adds evaluation tooling.
