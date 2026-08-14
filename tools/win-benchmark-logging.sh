#!/bin/bash
# A/B benchmark for whisper/ggml logging overhead on application startup
# (model load) and inference speed (transcription).
#
# Compares three --log modes in VoiceTyperBench:
#   off      - no-op log callback (baseline: zero logging overhead)
#   file     - file-based logging, WARN+ only (normal app behavior)
#   verbose  - file-based logging, all levels (app with VOICETYPER_VERBOSE=1)
#
# The bench.log file written in file/verbose mode mimics the app's debug.log
# (same level filtering, same mutex-protected fputs pattern as diag_write_log_line).
#
# Usage:
#   tools/win-benchmark-logging.sh [--cuda] [--threads N] [--warmup N] [--iterations N]
#
# Assumes a warm ccache. Reuses build/cpu if present, else configures fresh.

set -u

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR" || exit 1

BUILD_JOBS="${VOICETYPER_BUILD_JOBS:-$(nproc 2>/dev/null || echo 1)}"
GENERATOR="${VOICETYPER_CMAKE_GENERATOR:-Ninja}"
PLATFORM_FLAG=""
case "$GENERATOR" in
	Ninja) ;;
	*) PLATFORM_FLAG="-A x64" ;;
esac

MODEL_PATH="${VOICETYPER_MODEL_PATH:-stt_models/ggml-base.bin}"
AUDIO_PATH="${VOICETYPER_AUDIO_PATH:-bench/jfk.wav}"
THREADS="${VOICETYPER_BENCH_THREADS:-4}"
WARMUP="${VOICETYPER_BENCH_WARMUP:-3}"
ITERATIONS="${VOICETYPER_BENCH_ITERATIONS:-7}"
USE_CUDA=0

while [ "$#" -gt 0 ]; do
	case "$1" in
		--cuda) USE_CUDA=1; shift ;;
		--threads)
			[ "$#" -ge 2 ] || { echo "--threads requires a value" >&2; exit 2; }
			THREADS="$2"; shift 2 ;;
		--warmup)
			[ "$#" -ge 2 ] || { echo "--warmup requires a value" >&2; exit 2; }
			WARMUP="$2"; shift 2 ;;
		--iterations)
			[ "$#" -ge 2 ] || { echo "--iterations requires a value" >&2; exit 2; }
			ITERATIONS="$2"; shift 2 ;;
		--model)
			[ "$#" -ge 2 ] || { echo "--model requires a value" >&2; exit 2; }
			MODEL_PATH="$2"; shift 2 ;;
		--audio)
			[ "$#" -ge 2 ] || { echo "--audio requires a value" >&2; exit 2; }
			AUDIO_PATH="$2"; shift 2 ;;
		*) echo "Unknown argument: $1" >&2; exit 2 ;;
	esac
done

if [ "$USE_CUDA" -eq 1 ]; then
	BACKEND="cuda"
	CUDA_PLUGIN="ON"
	DEVICE="gpu"
else
	BACKEND="cpu"
	CUDA_PLUGIN="OFF"
	DEVICE="cpu"
fi

BUILD_DIR="$ROOT_DIR/build/logging-bench"
OUTPUT_BASE="$ROOT_DIR/build/logging-bench/out"
OUTPUT_DIR="$OUTPUT_BASE/Release_${BACKEND}"
BENCH_DIR="$OUTPUT_BASE/Bench_${BACKEND}"
BENCH_EXE="$BENCH_DIR/VoiceTyperBench.exe"
LOG_DIR="$ROOT_DIR/build/logging-bench/logs"

mkdir -p "$BUILD_DIR" "$LOG_DIR"

echo "=== Logging overhead benchmark ==="
echo "Generator: $GENERATOR  Jobs: $BUILD_JOBS  Threads: $THREADS"
echo "Backend: $BACKEND  Model: $MODEL_PATH  Audio: $AUDIO_PATH"
echo "Warmup: $WARMUP  Iterations: $ITERATIONS"
echo ""

# ---- Build ----

CUDA_CMAKE_ARGS=""
if [ "$USE_CUDA" -eq 1 ]; then
	if [ -z "${CUDA_PATH:-}" ] && [ -d "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.2" ]; then
		export CUDA_PATH="C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.2"
	fi
	if [ -n "${CUDA_PATH:-}" ]; then
		CUDA_CMAKE_ARGS="-DCUDAToolkit_ROOT=$CUDA_PATH"
	fi
fi

echo "--- Configuring ---"
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -G "$GENERATOR" $PLATFORM_FLAG \
	-DVOICETYPER_BUILD_CUDA_PLUGIN="$CUDA_PLUGIN" \
	-DVOICETYPER_BUILD_BENCH=ON \
	-DVOICETYPER_OUTPUT_BASE_DIR="$OUTPUT_BASE" \
	$CUDA_CMAKE_ARGS \
	> "$LOG_DIR/configure.log" 2>&1
if [ "$?" -ne 0 ]; then
	echo "CONFIGURE FAILED; tail of log:"
	tail -20 "$LOG_DIR/configure.log"
	exit 1
fi

echo "--- Building ---"
cmake --build "$BUILD_DIR" --config Release --parallel "$BUILD_JOBS" \
	> "$LOG_DIR/build.log" 2>&1
if [ "$?" -ne 0 ]; then
	echo "BUILD FAILED; tail of log:"
	tail -30 "$LOG_DIR/build.log"
	exit 1
fi

if [ ! -f "$BENCH_EXE" ]; then
	echo "Benchmark exe not found: $BENCH_EXE"
	exit 1
fi

# Copy DLLs next to bench exe so Windows can resolve them.
cp -u "$OUTPUT_DIR"/*.dll "$BENCH_DIR"/ 2>/dev/null || true

# For CUDA, also copy the cuda/ plugin folder.
if [ "$USE_CUDA" -eq 1 ]; then
	mkdir -p "$BENCH_DIR/cuda"
	cp -ru "$OUTPUT_DIR/cuda"/*.dll "$BENCH_DIR/cuda"/ 2>/dev/null || true
fi

echo ""
echo "--- Running benchmarks ---"

# ---- Run bench for each log mode ----

run_bench_mode() {
	local log_mode="$1"
	local label="$2"
	local run_log="$LOG_DIR/bench_${log_mode}.log"

	echo "  $label (--log $log_mode) ..."

	local bench_args=(
		"$BENCH_EXE"
		--model "$ROOT_DIR/$MODEL_PATH"
		--audio "$ROOT_DIR/$AUDIO_PATH"
		--device "$DEVICE"
		--warmup "$WARMUP"
		--iterations "$ITERATIONS"
		--threads "$THREADS"
		--log "$log_mode"
	)

	(cd "$BENCH_DIR" && "${bench_args[@]}") > "$run_log" 2>&1
	local status=$?
	if [ "$status" -ne 0 ]; then
		echo "    FAILED (exit $status); tail:"
		tail -10 "$run_log"
		eval "${log_mode}_JSON=''"
		return 1
	fi

	local json_line
	json_line=$(tr -d '\r' < "$run_log" | grep -E '^\{.*"transcribe_ms"' | tail -1)
	eval "${log_mode}_JSON=\"\$json_line\""

	if [ -z "$json_line" ]; then
		echo "    FAILED (no JSON output); tail:"
		tail -10 "$run_log"
		return 1
	fi

	# Report file size of bench.log for file/verbose modes
	local bench_log_path="$BENCH_DIR/bench.log"
	if [ -f "$bench_log_path" ]; then
		local log_bytes
		log_bytes=$(stat -c%s "$bench_log_path" 2>/dev/null || echo 0)
		echo "    bench.log size: $log_bytes bytes"
		rm -f "$bench_log_path"
	fi

	echo "    OK"
	return 0
}

run_bench_mode off      "Baseline (no logging)"
run_bench_mode file     "File logging (WARN+, normal app)"
run_bench_mode verbose  "File logging (all levels, verbose app)"

# ---- Summary ----

extract_json_field() {
	local json="$1"
	local field="$2"
	printf '%s' "$json" | sed -n "s/.*\"${field}\":\[\([^]]*\)\].*/\1/p"
}

extract_json_scalar() {
	local json="$1"
	local field="$2"
	printf '%s' "$json" | sed -n "s/.*\"${field}\":\([^,}]*\).*/\1/p"
}

compute_stats() {
	local arr="$1"
	if [ -z "$arr" ]; then
		printf "n=0 (no data)"
		return
	fi
	printf '%s' "$arr" | awk -F, '
		{
			n = NF
			sum = 0
			for (i = 1; i <= n; i++) { xs[i] = $i; sum += $i }
			mean = sum / n
			ssq = 0
			for (i = 1; i <= n; i++) { d = xs[i] - mean; ssq += d * d }
			stddev = sqrt(ssq / n)
			printf "n=%d mean=%.1fms +/-%.1fms", n, mean, stddev
		}'
}

echo ""
echo "==================== SUMMARY ===================="

for mode in off file verbose; do
	eval "json=\$${mode}_JSON"
	if [ -z "$json" ]; then
		printf "%-10s  FAILED\n" "$mode"
		continue
	fi

	load_ms=$(extract_json_scalar "$json" "model_load_ms")
	transcribe_arr=$(extract_json_field "$json" "transcribe_ms")
	stats=$(compute_stats "$transcribe_arr")

	printf "%-10s  model_load=%s ms  transcribe: %s\n" "$mode" "$load_ms" "$stats"
done

echo ""

# Compute deltas vs baseline
eval "off_json=\$off_JSON"
if [ -n "$off_json" ]; then
	off_load=$(extract_json_scalar "$off_json" "model_load_ms")
	off_transcribe=$(extract_json_field "$off_json" "transcribe_ms")

	for mode in file verbose; do
		eval "json=\$${mode}_JSON"
		if [ -z "$json" ]; then continue; fi

		load_ms=$(extract_json_scalar "$json" "model_load_ms")
		transcribe_arr=$(extract_json_field "$json" "transcribe_ms")

		load_delta=$(awk "BEGIN{printf \"%+.1f\", $load_ms - $off_load}")

		# Compute mean delta for transcribe
		off_mean=$(printf '%s' "$off_transcribe" | awk -F, '{s=0;for(i=1;i<=NF;i++)s+=$i;print s/NF}')
		mode_mean=$(printf '%s' "$transcribe_arr" | awk -F, '{s=0;for(i=1;i<=NF;i++)s+=$i;print s/NF}')
		trans_delta=$(awk "BEGIN{printf \"%+.1f\", $mode_mean - $off_mean}")

		printf "%s vs off:  model_load delta=%s ms  transcribe mean delta=%s ms\n" \
			"$mode" "$load_delta" "$trans_delta"
	done
fi

echo "================================================"
