#!/usr/bin/env bash
# Agent-drivable performance profiling for VoiceTyper (Windows / git bash).
#
# Builds (if needed) the instrumented variant (build/cpu-perf, VOICETYPER_PERF=ON)
# and runs the profiling tools, collecting results + summaries into
# build/profile-runs/<timestamp>/.
#
# Usage:
#   tools/profile.sh                     latency bench + transcription bench
#   tools/profile.sh --gui 30            also run the GUI app for 30s (idle profile)
#   tools/profile.sh --model PATH        transcription model (default: first in stt_models/)
#   tools/profile.sh --audio PATH        transcription wav, 16kHz mono (default bench/jfk.wav)
#   tools/profile.sh --latency-iters N   capture-latency iterations (default 10)
#   tools/profile.sh --bench-iters N     transcription iterations (default 3)
#
# Outputs (per run dir):
#   latency.json     VoiceTyperBench --mode capture-latency stdout
#   transcribe.json  VoiceTyperBench transcription stdout (mem/cpu/rtf fields)
#   gui-perf.json    perf.h JSON report from the GUI session (if --gui)
#   summary.txt      condensed key numbers

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

GUI_SECONDS=0
LATENCY_ITERS=10
BENCH_ITERS=3
MODEL=""
AUDIO="bench/jfk.wav"

while [[ $# -gt 0 ]]; do
	case "$1" in
		--gui) GUI_SECONDS="$2"; shift 2 ;;
		--model) MODEL="$2"; shift 2 ;;
		--audio) AUDIO="$2"; shift 2 ;;
		--latency-iters) LATENCY_ITERS="$2"; shift 2 ;;
		--bench-iters) BENCH_ITERS="$2"; shift 2 ;;
		*) echo "unknown option: $1" >&2; exit 2 ;;
	esac
done

PERF_BUILD_DIR=build/cpu-perf
PERF_BIN_DIR=build/perf/Release_cpu
BENCH_DIR=build/perf/Bench_cpu

if [[ -z "$MODEL" ]]; then
	MODEL="$(ls stt_models/ggml-*.bin 2>/dev/null | head -1 || true)"
fi
if [[ -z "$MODEL" ]]; then
	echo "no model found in stt_models/ (and --model not given)" >&2
	exit 1
fi
if [[ ! -f "$AUDIO" ]]; then
	echo "audio file not found: $AUDIO" >&2
	exit 1
fi

BAT="$(mktemp "${TMP:-/tmp}/vt_profile_build.XXXXXX.bat")"
cat > "$BAT" <<'EOF'
@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d E:\repos\VoiceTyper
if not exist build\cpu-perf\CMakeCache.txt (
	cmake -S . -B build/cpu-perf -G Ninja -DCMAKE_BUILD_TYPE=Release -DVOICETYPER_BUILD_BENCH=ON -DVOICETYPER_PERF=ON -DVOICETYPER_OUTPUT_BASE_DIR=E:/repos/VoiceTyper/build/perf || exit /b 1
)
cmake --build build/cpu-perf --target VoiceTyper VoiceTyperBench || exit /b 1
EOF
cmd //c "$(cygpath -w "$BAT" 2>/dev/null || echo "$BAT")" > /dev/null
rm -f "$BAT"

mkdir -p "$BENCH_DIR"
cp -u "$PERF_BIN_DIR"/*.dll "$BENCH_DIR"/ 2>/dev/null || cp "$PERF_BIN_DIR"/*.dll "$BENCH_DIR"/

RUN_DIR="build/profile-runs/$(date +%Y%m%d-%H%M%S)"
mkdir -p "$RUN_DIR"

echo "== capture latency ($LATENCY_ITERS iterations) =="
"$BENCH_DIR/VoiceTyperBench.exe" --mode capture-latency --iterations "$LATENCY_ITERS" \
	> "$RUN_DIR/latency.json"
cat "$RUN_DIR/latency.json"

echo
echo "== transcription ($MODEL, $BENCH_ITERS iterations) =="
"$BENCH_DIR/VoiceTyperBench.exe" --audio "$AUDIO" --model "$MODEL" --vad on \
	--iterations "$BENCH_ITERS" > "$RUN_DIR/transcribe.json"
cat "$RUN_DIR/transcribe.json"

GUI_REPORT=""
if [[ "$GUI_SECONDS" -gt 0 ]]; then
	echo
	echo "== gui session (${GUI_SECONDS}s, VOICETYPER_PERF=1) =="
	rm -f "$PERF_BIN_DIR"/voicetyper-perf-*.json
	(VOICETYPER_PERF=1 "$PERF_BIN_DIR/VoiceTyper.exe" >/dev/null 2>&1 &)
	sleep "$GUI_SECONDS"
	PID="$(tasklist //fi "IMAGENAME eq VoiceTyper.exe" //fo csv //nh | head -1 | cut -d'"' -f4 || true)"
	if [[ -n "$PID" ]]; then
		taskkill //PID "$PID" //F > /dev/null 2>&1 || true
	fi
	sleep 1
	GUI_REPORT="$(ls -t "$PERF_BIN_DIR"/voicetyper-perf-*.json 2>/dev/null | head -1 || true)"
	if [[ -n "$GUI_REPORT" ]]; then
		cp "$GUI_REPORT" "$RUN_DIR/gui-perf.json"
		echo "report: $RUN_DIR/gui-perf.json"
	fi
fi

{
	echo "run: $RUN_DIR  ($(date))"
	echo "model: $MODEL   audio: $AUDIO"
	echo
	echo "-- latency (ms) --"
	python - "$RUN_DIR/latency.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
for k in ("open_ms", "start_ms", "first_audio_ms", "stop_ms"):
	if k in d:
		s = d[k]
		print(f"{k:14s} min {s['min']:8.2f}  med {s['med']:8.2f}  avg {s['avg']:8.2f}  max {s['max']:8.2f}")
print("device:", d.get("device_name"))
PY
	echo
	echo "-- transcription --"
	python - "$RUN_DIR/transcribe.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
for k in ("model_load_ms", "cpu_model_load_ms", "mem_before_private_mb", "mem_after_private_mb",
		"mem_model_private_mb", "mem_model_working_mb", "peak_private_mb",
		"cpu_transcribe_avg_ms", "rtf_avg", "rtf_best"):
	if k in d:
		print(f"{k:26s} {d[k]}")
PY
	if [[ -n "$GUI_REPORT" ]]; then
		echo
		echo "-- gui session --"
		python - "$RUN_DIR/gui-perf.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
print("uptime_ms", round(d["uptime_ms"]), "cpu_total_ms", round(d["cpu_total_ms"], 1))
print("peak_ws_mb", round(d["peak_working_set_bytes"] / 2**20, 1),
	"peak_priv_mb", round(d["peak_private_bytes"] / 2**20, 1))
t = d["tick"]
print(f"tick avg_ms {t['avg_ms']:.2f} max_ms {t['max_ms']:.2f} count {t['count']}")
s = d["samples"]
if s:
	idle = s[len(s)//2:]
	avg = sum(x["cpu_pct"] for x in idle) / len(idle)
	print(f"idle_cpu_pct(one core=100) avg {avg:.2f} over {len(idle)} samples")
PY
	fi
} | tee "$RUN_DIR/summary.txt"

echo
echo "artifacts in $RUN_DIR"
