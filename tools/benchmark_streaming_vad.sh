#!/bin/bash

set -u

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AUDIO_PATH=""
MODEL_PATH="stt_models/ggml-base.en.bin"
VAD_MODEL_PATH="vad_models/ggml-silero-v5.1.2.bin"
EXPECTED_TEXT=""
HAS_EXPECTED_TEXT=0
LABEL=""
INCLUDE_CUDA=0
CUDA_DEVICE=0
WARMUP_COUNT=1
ITERATION_COUNT=5
THREAD_COUNT="$(nproc 2>/dev/null || echo 1)"
OUTPUT_PATH="build/bench/streaming_vad_results.jsonl"
APPEND_OUTPUT=0
BUILD_JOBS="${VOICETYPER_BUILD_JOBS:-$(nproc 2>/dev/null || echo 1)}"

usage() {
	cat <<EOF
Usage: tools/benchmark_streaming_vad.sh --audio <path> [options]

Options:
  --model <path>          Default: stt_models/ggml-base.en.bin
  --vad-model <path>      Default: vad_models/ggml-silero-v5.1.2.bin
  --expected-text <text>  Optional exact expected transcription
  --label <name>          Test case label (default: audio basename without extension)
  --cuda                  Build and run with CUDA
  --cuda-device <index>   Default: 0
  --warmup <count>        Default: 1
  --iterations <count>    Default: 5
  --threads <count>       Default: hardware concurrency
  --output <path>         Default: build/bench/streaming_vad_results.jsonl
  --append                Append instead of overwrite
EOF
}

while [ "$#" -gt 0 ]; do
	case "$1" in
		--audio)
			[ "$#" -ge 2 ] || { echo "--audio requires a value" >&2; exit 2; }
			AUDIO_PATH="$2"
			shift 2
			;;
		--model)
			[ "$#" -ge 2 ] || { echo "--model requires a value" >&2; exit 2; }
			MODEL_PATH="$2"
			shift 2
			;;
		--vad-model)
			[ "$#" -ge 2 ] || { echo "--vad-model requires a value" >&2; exit 2; }
			VAD_MODEL_PATH="$2"
			shift 2
			;;
		--expected-text)
			[ "$#" -ge 2 ] || { echo "--expected-text requires a value" >&2; exit 2; }
			EXPECTED_TEXT="$2"
			HAS_EXPECTED_TEXT=1
			shift 2
			;;
		--label)
			[ "$#" -ge 2 ] || { echo "--label requires a value" >&2; exit 2; }
			LABEL="$2"
			shift 2
			;;
		--cuda)
			INCLUDE_CUDA=1
			shift
			;;
		--cuda-device)
			[ "$#" -ge 2 ] || { echo "--cuda-device requires a value" >&2; exit 2; }
			CUDA_DEVICE="$2"
			shift 2
			;;
		--warmup)
			[ "$#" -ge 2 ] || { echo "--warmup requires a value" >&2; exit 2; }
			WARMUP_COUNT="$2"
			shift 2
			;;
		--iterations)
			[ "$#" -ge 2 ] || { echo "--iterations requires a value" >&2; exit 2; }
			ITERATION_COUNT="$2"
			shift 2
			;;
		--threads)
			[ "$#" -ge 2 ] || { echo "--threads requires a value" >&2; exit 2; }
			THREAD_COUNT="$2"
			shift 2
			;;
		--output)
			[ "$#" -ge 2 ] || { echo "--output requires a value" >&2; exit 2; }
			OUTPUT_PATH="$2"
			shift 2
			;;
		--append)
			APPEND_OUTPUT=1
			shift
			;;
		--help|-h)
			usage
			exit 0
			;;
		*)
			echo "Unknown argument: $1" >&2
			usage >&2
			exit 2
			;;
	esac
done

if [ -z "$AUDIO_PATH" ]; then
	echo "--audio is required" >&2
	usage >&2
	exit 2
fi

if [ -z "$LABEL" ]; then
	LABEL="$(basename "$AUDIO_PATH")"
	LABEL="${LABEL%.*}"
fi

case "$OUTPUT_PATH" in
	/*|[A-Za-z]:*) ;;
	*) OUTPUT_PATH="$ROOT_DIR/$OUTPUT_PATH" ;;
esac

mkdir -p "$(dirname "$OUTPUT_PATH")"
if [ "$APPEND_OUTPUT" -eq 1 ]; then
	touch "$OUTPUT_PATH"
else
	: > "$OUTPUT_PATH"
fi

cd "$ROOT_DIR" || exit 1

if [ "$INCLUDE_CUDA" -eq 1 ]; then
	BACKEND="cuda"
	USE_CUDA="ON"
else
	BACKEND="cpu"
	USE_CUDA="OFF"
fi

BUILD_DIR="$ROOT_DIR/build/bench/streaming_vad/build"
OUTPUT_BASE="$ROOT_DIR/build/bench/streaming_vad/out"
OUTPUT_DIR="$OUTPUT_BASE/Release_${BACKEND}"
LOG_DIR="$ROOT_DIR/build/bench/streaming_vad/logs"
BENCH_EXE="$OUTPUT_DIR/VoiceTyperBench.exe"

mkdir -p "$BUILD_DIR" "$LOG_DIR"

now_ms() {
	local value
	value="$(date +%s%3N)"
	if [[ "$value" == *N ]]; then
		value="$(($(date +%s) * 1000))"
	fi
	echo "$value"
}

json_escape() {
	local value="$1"
	value="${value//\\/\\\\}"
	value="${value//\"/\\\"}"
	value="${value//$'\r'/\\r}"
	value="${value//$'\n'/\\n}"
	value="${value//$'\t'/\\t}"
	printf '%s' "$value"
}

summarize_log() {
	local log_path="$1"
	local summary=""
	if [ -f "$log_path" ]; then
		summary="$(tr '\r\n\t' '   ' < "$log_path")"
	fi
	printf '%s' "${summary:0:500}"
}

run_timed() {
	local log_path="$1"
	shift
	local start_ms
	local end_ms
	start_ms="$(now_ms)"
	"$@" > "$log_path" 2>&1
	local status=$?
	end_ms="$(now_ms)"
	MEASURED_MS=$((end_ms - start_ms))
	return "$status"
}

write_failure_row() {
	local variant="$1"
	local phase="$2"
	local log_path="$3"
	local error_summary
	error_summary="$(json_escape "$(summarize_log "$log_path")")"
	printf '{"label":"%s","variant":"%s","backend":"%s","failed_phase":"%s","error":"%s"}\n' \
		"$LABEL" "$variant" "$BACKEND" "$phase" "$error_summary" >> "$OUTPUT_PATH"
}

write_success_row() {
	local variant="$1"
	local runtime_json="$2"
	local runtime_body="${runtime_json#\{}"
	printf '{"label":"%s","variant":"%s","backend":"%s",%s\n' \
		"$LABEL" "$variant" "$BACKEND" "$runtime_body" >> "$OUTPUT_PATH"
}

run_bench() {
	local variant="$1"
	local mode="$2"
	local vad_state="$3"
	local run_stdout="$LOG_DIR/${variant}_stdout.log"
	local run_stderr="$LOG_DIR/${variant}_stderr.log"

	echo "== $variant =="
	local bench_args=(
		"$BENCH_EXE"
		--model "$MODEL_PATH"
		--audio "$AUDIO_PATH"
		--vad-model "$VAD_MODEL_PATH"
		--mode "$mode"
		--vad "$vad_state"
		--warmup "$WARMUP_COUNT"
		--iterations "$ITERATION_COUNT"
		--threads "$THREAD_COUNT"
		--cuda-device "$CUDA_DEVICE"
	)

	if [ "$HAS_EXPECTED_TEXT" -eq 1 ]; then
		bench_args+=(--expected-text "$EXPECTED_TEXT")
	fi

	"${bench_args[@]}" > "$run_stdout" 2> "$run_stderr"
	local run_status=$?
	if [ "$run_status" -ne 0 ]; then
		write_failure_row "$variant" "run" "$run_stderr"
		return
	fi

	local runtime_json
	runtime_json="$(tr -d '\r\n' < "$run_stdout")"
	case "$runtime_json" in
		\{*) write_success_row "$variant" "$runtime_json" ;;
		*) write_failure_row "$variant" "run_parse" "$run_stdout" ;;
	esac
}

cmake_args=(
	cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -G "Visual Studio 17 2022" -A x64
	-DVOICETYPER_CUDA="$USE_CUDA"
	-DVOICETYPER_OUTPUT_BASE_DIR="$OUTPUT_BASE"
)

if [ "$USE_CUDA" = "ON" ]; then
	if [ -z "${CUDA_PATH:-}" ] && [ -d "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.2" ]; then
		export CUDA_PATH="C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.2"
	fi
	if [ -n "${CUDA_PATH:-}" ]; then
		cmake_args+=(-DCUDAToolkit_ROOT="$CUDA_PATH")
	fi
fi

configure_log="$LOG_DIR/configure.log"
build_log="$LOG_DIR/build.log"

if ! run_timed "$configure_log" "${cmake_args[@]}"; then
	echo "CMake configure failed (see $configure_log)" >&2
	write_failure_row "build" "configure" "$configure_log"
	exit 1
fi

if ! run_timed "$build_log" cmake --build "$BUILD_DIR" --config Release --parallel "$BUILD_JOBS"; then
	echo "Build failed (see $build_log)" >&2
	write_failure_row "build" "build" "$build_log"
	exit 1
fi

if [ ! -f "$BENCH_EXE" ]; then
	echo "Benchmark executable not found: $BENCH_EXE" >&2
	write_failure_row "build" "missing_exe" "$build_log"
	exit 1
fi

run_bench "record-vad-on"      record    on
run_bench "streaming-vad-on"   streaming on
run_bench "streaming-vad-off"  streaming off

echo "Results written to $OUTPUT_PATH"
