---
name: voicetyper-profiling
description: Use when profiling or measuring VoiceTyper performance — CPU time, memory usage (working set / private bytes), transcription speed (RTF), or recording start latency. Covers tools/profile.sh, VoiceTyperBench modes, the VOICETYPER_PERF build flag, and reading voicetyper-perf JSON reports.
---

# VoiceTyper performance profiling

Agent-drivable measurement infrastructure for CPU, memory, and audio-capture
latency. Windows-first (that is where the product lives); the interfaces are
portable but Linux implementations are deferred.

## One-command workflow

```
tools/profile.sh                      # capture-latency bench + transcription bench
tools/profile.sh --gui 30             # + run the GUI app 30s for an idle/startup profile
tools/profile.sh --model stt_models/ggml-base.bin --audio bench/jfk.wav --bench-iters 5
```

The script builds the instrumented variant if needed (`build/cpu-perf`,
`-DVOICETYPER_PERF=ON -DVOICETYPER_BUILD_BENCH=ON`, outputs in `build/perf/`),
runs the benches, and writes everything to `build/profile-runs/<timestamp>/`:
`latency.json`, `transcribe.json`, optional `gui-perf.json`, and `summary.txt`
(condensed numbers — read this first).

## What is measured where

- **Record-start latency** (`latency.json`): `open_ms` (request ->
  waveInOpen done), `start_ms` (request -> waveInStart returned, i.e. the
  device is capturing — this is the audio actually LOST window), `first_audio_ms`
  (request -> first samples appended; dominated by the 100 ms WaveIn buffer
  granularity = delivery/feedback latency, not lost audio), `stop_ms`
  (stop request -> device closed). Min/med/avg/max per metric.
- **Transcription CPU/memory** (`transcribe.json`): `model_load_ms`,
  `cpu_model_load_ms`, `mem_model_private_mb` (committed) vs
  `mem_model_working_mb` (touched) — the gap between these two is the
  over-commit story, `peak_private_mb`, `cpu_transcribe_ms` (per iteration,
  process CPU), `rtf_avg`/`rtf_best` (audio-seconds per wall-second; higher is
  better), plus WER fields with `--expected-text`.
- **GUI app lifecycle** (`gui-perf.json`): timestamped events (`app_init`,
  `ui_backends_init_done`, `first_frame_presented`, `model_load`,
  `audio_device_open`, `audio_first_samples`, `transcribe`, `paste`, ...),
  each with cpu/ws/private snapshots; periodic samples with
  `cpu_pct` (100 = one core); `tick` stats = app-update cadence at 100 Hz,
  whose `max_ms` bounds the hotkey-poll detection delay.

## Manual invocation

```
PATH="$PWD/build/perf/Release_cpu:$PATH" build/perf/Bench_cpu/VoiceTyperBench.exe \
    --mode capture-latency --iterations 10
build/perf/Bench_cpu/VoiceTyperBench.exe --audio bench/jfk.wav \
    --model stt_models/ggml-base.bin --vad on --iterations 3
VOICETYPER_PERF=1 VOICETYPER_PERF_HZ=10 build/perf/Release_cpu/VoiceTyper.exe &   # writes
    # build/perf/Release_cpu/voicetyper-perf-<pid>.json, rewritten every 5s
    # (survives taskkill; kill via: taskkill //PID <pid> //F)
```

`--mode streaming` chunks the audio like the streaming pipeline;
`--mode capture-latency` needs no model/audio. `--audio-device <index>` picks
a capture device (default: system default).

## Build flag

Instrumentation is compile-gated: `VOICETYPER_PERF=OFF` (default, and all
release flows) compiles every probe to a no-op — zero perf code ships. The
bench binary is always instrumented (dev tool, never shipped). Source:
`src/perf.h`; app call sites live in `imgui_main_windows.cpp`,
`audio_pipeline.h`, `runtime_control.h`, `platform_win32.h`.

## Baseline numbers to compare against

2026-09-19, dev box (Ryzen, 20 hw threads, JLab USB mic, ggml-base CPU):
- latency: open ~14.4 ms med, start ~14.6 ms med, first_audio ~142 ms med,
  stop ~45 ms med
- transcription: load 117 ms / 793 MB committed (198 MB touched, model file
  is 148 MB), peak 928 MB, 11.6 s CPU per 11 s utterance (~0.63 s wall, RTF ~17)
- GUI idle (window visible): ~5-8% of one core, 100 Hz tick avg 10 ms
  (max stalls 20-80 ms), no-model footprint ~65 MB private

## Known findings / likely optimizations (as of the baseline above)

- ggml/whisper commits ~5x the model size in private bytes while touching
  ~1.3x — compute-buffer sizing is the lever.
- WaveIn 100 ms buffers delay first delivery ~100 ms (feedback latency) and
  waveInReset discards the trailing partial buffer on stop (up to 100 ms of
  final audio lost — likely clips the last word).
- Device open (~14 ms) + hotkey poll (up to tick max, ~20-80 ms stalls) form
  the lost-audio window for the first word.
- Idle GUI burns ~5-8% of a core while visible (render loop at monitor
  refresh).
