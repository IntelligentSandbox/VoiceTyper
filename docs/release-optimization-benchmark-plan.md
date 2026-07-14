# Release Optimization Benchmark Plan

## Goal
Design and implement repeatable Release optimization benchmarks before deciding whether to enable LTO/IPO.

The benchmark should compare app-level IPO and whisper.cpp/GGML LTO settings using fixed model/audio inputs, then
record build metrics, binary sizes, runtime latency, and transcription correctness in JSONL.

## Scope
- Add a small non-GUI benchmark executable.
- Extract only shared Whisper parameter creation and transcribe-to-string logic from the app path.
- Add a benchmark runner script that builds and measures Release variants.
- Keep normal app behavior unchanged.
- Do not commit benchmark model files.
- A single small public-domain audio fixture (`bench/jfk.wav`) is committed for repeatable runs;
  all other audio inputs remain user-supplied via `--audio`.

## Benchmark Inputs
- The runner requires `--audio <path>`; `bench/jfk.wav` is committed as a default-capable fixture.
- `bench/jfk.wav` is whisper.cpp's public-domain JFK excerpt (16 kHz mono PCM s16le, ~11 s).
  Expected transcription: "And so, my fellow Americans, ask not what your country can do for you, ask what you can do for your country."
- The runner defaults `--model` to `stt_models/ggml-base.en.bin`.
- The target model for initial testing is `base.en`.
- The benchmark executable supports only 16 kHz mono WAV input for v1.
- Supported v1 WAV encodings:
	- PCM signed 16-bit little-endian.
	- IEEE float32 little-endian, if straightforward to implement.
- Resampling and channel conversion are out of scope for v1.
- Unsupported audio formats should fail with a clear error.

## Benchmark CLI
The benchmark executable should focus only on runtime measurement and print one JSON object to stdout.

Example:

```bash
VoiceTyperBench.exe \
	--model stt_models/ggml-base.en.bin \
	--audio bench/input.wav \
	--expected-text "hello world" \
	--warmup 1 \
	--iterations 5 \
	--threads 8
```

Supported arguments:
- `--model <path>`: Whisper model path.
- `--audio <path>`: required WAV input path.
- `--expected-text <text>`: optional expected transcription.
- `--warmup <count>`: default `1`.
- `--iterations <count>`: default `5`.
- `--threads <count>`: default to hardware concurrency.
- `--cuda-device <index>`: default `0`, used by CUDA builds.

## Shared Transcription Core
Add a small shared header, likely `src/transcription_core.h`, used by both the app and benchmark.

Responsibilities:
- Create `whisper_full_params` using the same defaults as the app.
- Run `whisper_full` on a float PCM chunk.
- Return transcription text as a string.
- Avoid text injection or platform-specific behavior.

The app's audio pipeline should continue to own capture and injection:
- Capture audio.
- Call the shared transcribe-to-string function.
- Inject returned text into the target window.

The benchmark should:
- Load WAV into float PCM.
- Load the model through the existing Whisper wrapper.
- Call the same shared transcribe-to-string function.
- Measure load and transcription timings.
- Emit runtime JSON.

## Build Variants
The runner builds CPU variants by default:

| Variant | App IPO | GGML LTO |
| --- | --- | --- |
| `cpu-baseline` | off | off |
| `cpu-app-ipo` | on | off |
| `cpu-ggml-lto` | off | on |
| `cpu-app-ipo-ggml-lto` | on | on |

With `--cuda`, the runner also builds CUDA variants:

| Variant | App IPO | GGML LTO |
| --- | --- | --- |
| `cuda-baseline` | off | off |
| `cuda-app-ipo` | on | off |
| `cuda-ggml-lto` | off | on |
| `cuda-app-ipo-ggml-lto` | on | on |

CUDA should be opt-in so CPU-only machines can run the benchmark.

## CMake Requirements
Add CMake controls for:
- App IPO/LTO, likely via a `VOICETYPER_APP_IPO` option.
- GGML LTO passthrough, using whisper.cpp/GGML's `GGML_LTO` option.

Add a console benchmark target, likely `VoiceTyperBench`, linked against `whisper` and the same needed include paths.

The benchmark target should be output beside the app in each Release variant output directory.

## Runner Script
Add `tools/benchmark_release_optimization.sh`.

Runner defaults:
- CPU variants only.
- `--model stt_models/ggml-base.en.bin`.
- `--warmup 1`.
- `--iterations 5`.
- `--threads <hardware concurrency>`.
- `--output build/bench/results.jsonl`.
- Overwrite the output file by default.
- Support `--append` to append to an existing JSONL file.
- Use `VOICETYPER_BUILD_JOBS` when set; otherwise fall back to `nproc` or `1`.
- Delete only benchmark-owned build directories under `build/bench/`.

Runner options:
- `--audio <path>`: required.
- `--model <path>`: optional, defaults to `stt_models/ggml-base.en.bin`.
- `--expected-text <text>`: optional.
- `--cuda`: include CUDA variants.
- `--cuda-device <index>`: default `0`.
- `--warmup <count>`: default `1`.
- `--iterations <count>`: default `5`.
- `--threads <count>`: default hardware concurrency.
- `--output <path>`: default `build/bench/results.jsonl`.
- `--append`: append instead of overwrite.
- `--incremental`: measure incremental rebuild after modifying one source file.
- `--incremental-source <path>`: default `src/imgui_main.cpp`.

## Build Metrics
For each variant, measure both cold and warm build behavior.

Cold build:
- Delete that variant's benchmark build directory under `build/bench/`.
- Run CMake configure and measure `cold_configure_ms`.
- Run Release build and measure `cold_build_ms`.

Warm build:
- Re-run CMake configure in the existing build directory and measure `warm_configure_ms`.
- Re-run the Release build with no source changes and measure `warm_build_ms`.

This measures cached/no-op build-system behavior for warm builds. It does not attempt to measure incremental source rebuilds.

## Runtime Metrics
The benchmark executable should measure:
- `model_load_ms`.
- One or more warmup transcription runs, excluded from measured timings.
- `transcribe_ms` array for measured iterations.
- Final transcription text.
- Optional expected text and match status.

Timing should use high-resolution clocks internally and report milliseconds in JSON.

## Correctness Comparison
When `--expected-text` is provided:
- Normalize line endings from CRLF/CR to LF.
- Trim outer whitespace.
- Compare exactly after normalization and trimming.
- Emit `expected_text_match: true` or `false`.

When `--expected-text` is omitted:
- Emit the transcription text.
- Omit the match field or set it to `null`.

## JSONL Output
Each variant should produce one JSONL record enriched by the runner with build metrics, binary sizes, variant metadata,
and benchmark runtime metrics.

Example:

```json
{"variant":"cpu-baseline","backend":"cpu","app_ipo":false,"ggml_lto":false,"cold_configure_ms":1234,"cold_build_ms":60000,"warm_configure_ms":300,"warm_build_ms":900,"incremental_build_ms":1200,"voice_typer_exe_bytes":1234567,"bench_exe_bytes":765432,"model_load_ms":820,"transcribe_ms":[410,399,402,405,401],"text":"hello world","expected_text":"hello world","expected_text_match":true}
```

If a variant fails to configure, build, or run:
- Write a failed JSONL row with variant metadata, the phase that failed, and a short error summary.
- Continue to the next variant.

## Incremental Rebuild Metrics
When `--incremental` is passed, the runner performs an additional build after the warm build:

- Back up the configured `--incremental-source` file (default `src/imgui_main.cpp`).
- Append a single newline to trigger recompilation of that translation unit.
- Run the Release build and measure `incremental_build_ms`.
- Restore the original file content from the backup.

This measures the realistic dev-iteration cost: one TU recompiled plus relinking.
With LTO/IPO enabled, the relink step re-runs whole-program optimization across all IR objects,
which is where the per-change overhead of LTO/IPO is concentrated.

## Binary Size Metrics
Record output sizes after the cold build:
- App executable size.
- Benchmark executable size.
- Relevant generated static libraries or DLLs if they are present in the variant output/build directory.

Keep size collection pragmatic in v1. At minimum, record the app and benchmark executable byte sizes.

## Implementation Order
1. Add CMake options for app IPO and GGML LTO passthrough.
2. Extract shared Whisper parameter and transcribe-to-string logic.
3. Update the app audio pipeline to use the shared transcribe-to-string helper before injection.
4. Add the benchmark executable.
5. Add strict 16 kHz mono WAV loading with PCM s16 and float32 support.
6. Add benchmark JSON output and expected-text comparison.
7. Add the runner script and JSONL enrichment.
8. Verify CPU Release build.
9. Verify CUDA Release build when CUDA is available.
10. Run at least one CPU benchmark against `base.en` and a fixed WAV file.

## Out Of Scope For V1
- Committed Whisper model files.
- Additional committed audio fixtures beyond `bench/jfk.wav`.
- Audio resampling.
- Stereo-to-mono conversion.
- GUI automation.
- Statistical analysis beyond recording per-iteration timings.
