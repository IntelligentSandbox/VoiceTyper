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

2026-09-24, dev box (Ryzen, 20 hw threads, JLab USB mic, ggml-base CPU), after the
capture/GUI/inference tuning session:
- latency: open ~0.2 ms med (warm device; ~25-60 ms once on first take or device
  switch), start ~0.7 ms med, first_audio ~17 ms med, stop ~16-24 ms med,
  tail_gap ~0 ms (drain-after-reset; slightly negative = stop-request->reset
  continuation audio is preserved)
- transcription: load ~250 ms / ~717 MB committed (198 MB touched, model file
  is 148 MB), peak ~820 MB, ~4.7-7.3 s CPU per 11 s utterance depending on
  load (audio_ctx-capped), RTF ~18-40
- GUI idle (window visible): ~0.5-1.5% of a core (idle render throttle at 10 Hz
  after 1 s without input); 100 Hz tick avg 10 ms, p95/p99 10/11 ms, max ~14-16 ms
  (OS scheduling noise; Present(0) since 2026-09-27 — the vsync block is gone);
  no-model footprint ~65 MB private

Pre-tuning 2026-09-19 numbers, for reference on the changes made that day+next:
open ~14-24 ms, first_audio ~142 ms, tail loss up to 100 ms race, transcription
11.6 s CPU / RTF ~16 / 793 MB committed, idle 5-8% of a core, tick max 20-80 ms.

## Known findings / likely optimizations (as of the baseline above)

- Remaining whisper commit (~717 MB) is dominated by init-time worst-case 30 s
  conv/encode/cross/decode sched buffers (~520 MB) + weights + F16 kv caches —
  needs a whisper.cpp patch or upstream bump to shrink further.
- whisper sometimes decodes a trailing near-empty window into a "[BLANK_AUDIO]"
  segment (observed on GPU with audio_ctx caps; filtered in transcription_core).
- Tick-stall fix 2026-09-27: Present(1,0) blocked the tick/render thread on
  vblank whenever the render schedule drifted out of phase (rare but large
  outliers; one 30 s idle session measured max 33 ms, historical 13-27 ms).
  render_frame now uses Present(0) — tear-free through DWM composition of the
  windowed blt-model chain — with the existing high-res-timer pacing; an
  input-wake render is capped at one refresh interval since the last present
  (the rate limit vsync used to provide). After: max ~14-16 ms = scheduling
  noise floor; p99 unchanged at ~11 ms. perf.h tick stats now also report
  p50/p95/p99 (per-ms histogram).
- Bench DLL gotcha: VoiceTyperBench.exe loads ggml/whisper DLLs from its own
  directory first — copy fresh DLLs into build/perf/Bench_cpu after rebuilding,
  or you measure stale code.
