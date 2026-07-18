#!/bin/bash

set -u

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AUDIO_PATH=""
MODEL_PATH="stt_models/ggml-base.en.bin"
EXPECTED_TEXT=""
HAS_EXPECTED_TEXT=0
INCLUDE_CUDA=0
CUDA_DEVICE=0
WARMUP_COUNT=1
ITERATION_COUNT=5
THREAD_COUNT="$(nproc 2>/dev/null || echo 1)"
OUTPUT_PATH="build/bench/results.jsonl"
APPEND_OUTPUT=0
INCREMENTAL=0
INCREMENTAL_SOURCE="src/imgui_main.cpp"
BUILD_JOBS="${VOICETYPER_BUILD_JOBS:-$(nproc 2>/dev/null || echo 1)}"

usage() {
	cat <<EOF
Usage: tools/benchmark_release_optimization.sh --audio <path> [options]

Options:
  --model <path>          Default: stt_models/ggml-base.en.bin
  --expected-text <text>  Optional exact expected transcription
  --cuda                  Include CUDA variants
  --cuda-device <index>   Default: 0
  --warmup <count>        Default: 1
  --iterations <count>    Default: 5
  --threads <count>       Default: hardware concurrency
  --output <path>         Default: build/bench/results.jsonl
  --append                Append instead of overwrite
  --incremental           Measure incremental rebuild after touching one source file
  --incremental-source <path>  Default: src/imgui_main.cpp
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
		--expected-text)
			[ "$#" -ge 2 ] || { echo "--expected-text requires a value" >&2; exit 2; }
			EXPECTED_TEXT="$2"
			HAS_EXPECTED_TEXT=1
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
		--incremental)
			INCREMENTAL=1
			shift
			;;
		--incremental-source)
			[ "$#" -ge 2 ] || { echo "--incremental-source requires a value" >&2; exit 2; }
			INCREMENTAL_SOURCE="$2"
			shift 2
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

if [ "$INCREMENTAL" -eq 1 ] && [ ! -f "$ROOT_DIR/$INCREMENTAL_SOURCE" ]; then
	echo "--incremental-source not found: $INCREMENTAL_SOURCE" >&2
	exit 2
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

bool_json() {
	if [ "$1" = "ON" ]; then
		printf 'true'
	else
		printf 'false'
	fi
}

file_size_json() {
	if [ -f "$1" ]; then
		stat -c%s "$1"
	else
		printf 'null'
	fi
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
	local backend="$2"
	local app_ipo="$3"
	local ggml_lto="$4"
	local phase="$5"
	local log_path="$6"
	local error_summary
	error_summary="$(json_escape "$(summarize_log "$log_path")")"
	printf '{"variant":"%s","backend":"%s","app_ipo":%s,"ggml_lto":%s,' \
		"$variant" "$backend" "$(bool_json "$app_ipo")" "$(bool_json "$ggml_lto")" >> "$OUTPUT_PATH"
	printf '"cold_configure_ms":%s,"cold_build_ms":%s,' "$COLD_CONFIGURE_MS" "$COLD_BUILD_MS" >> "$OUTPUT_PATH"
	printf '"warm_configure_ms":%s,"warm_build_ms":%s,' "$WARM_CONFIGURE_MS" "$WARM_BUILD_MS" >> "$OUTPUT_PATH"
	printf '"incremental_build_ms":%s,' "$INCREMENTAL_BUILD_MS" >> "$OUTPUT_PATH"
	printf '"voice_typer_exe_bytes":%s,"bench_exe_bytes":%s,' \
		"$VOICE_TYPER_EXE_BYTES" "$BENCH_EXE_BYTES" >> "$OUTPUT_PATH"
	printf '"failed_phase":"%s","error":"%s"}\n' "$phase" "$error_summary" >> "$OUTPUT_PATH"
}

write_success_row() {
	local variant="$1"
	local backend="$2"
	local app_ipo="$3"
	local ggml_lto="$4"
	local runtime_json="$5"
	local runtime_body="${runtime_json#\{}"
	printf '{"variant":"%s","backend":"%s","app_ipo":%s,"ggml_lto":%s,' \
		"$variant" "$backend" "$(bool_json "$app_ipo")" "$(bool_json "$ggml_lto")" >> "$OUTPUT_PATH"
	printf '"cold_configure_ms":%s,"cold_build_ms":%s,' "$COLD_CONFIGURE_MS" "$COLD_BUILD_MS" >> "$OUTPUT_PATH"
	printf '"warm_configure_ms":%s,"warm_build_ms":%s,' "$WARM_CONFIGURE_MS" "$WARM_BUILD_MS" >> "$OUTPUT_PATH"
	printf '"incremental_build_ms":%s,' "$INCREMENTAL_BUILD_MS" >> "$OUTPUT_PATH"
	printf '"voice_typer_exe_bytes":%s,"bench_exe_bytes":%s,%s\n' \
		"$VOICE_TYPER_EXE_BYTES" "$BENCH_EXE_BYTES" "$runtime_body" >> "$OUTPUT_PATH"
}

run_variant() {
	local variant="$1"
	local backend="$2"
	local app_ipo="$3"
	local ggml_lto="$4"
	local use_cuda="$5"
	local variant_dir="$ROOT_DIR/build/bench/$variant"
	local build_dir="$variant_dir/build"
	local output_base="$variant_dir/out"
	local output_dir="$output_base/Release_${backend}"
	local bench_dir="$output_base/Bench_${backend}"
	local log_dir="$variant_dir/logs"
	local configure_log="$log_dir/cold_configure.log"
	local build_log="$log_dir/cold_build.log"
	local warm_configure_log="$log_dir/warm_configure.log"
	local warm_build_log="$log_dir/warm_build.log"
	local incremental_build_log="$log_dir/incremental_build.log"
	local run_stdout="$log_dir/runtime_stdout.log"
	local run_stderr="$log_dir/runtime_stderr.log"

	echo "== $variant =="
	rm -rf "$variant_dir"
	mkdir -p "$build_dir" "$log_dir"

	COLD_CONFIGURE_MS=null
	COLD_BUILD_MS=null
	WARM_CONFIGURE_MS=null
	WARM_BUILD_MS=null
	INCREMENTAL_BUILD_MS=null
	VOICE_TYPER_EXE_BYTES=null
	BENCH_EXE_BYTES=null

	local cmake_args=(
		cmake -S "$ROOT_DIR" -B "$build_dir" -G "Visual Studio 17 2022" -A x64
		-DVOICETYPER_CUDA="$use_cuda"
		-DVOICETYPER_APP_IPO="$app_ipo"
		-DGGML_LTO="$ggml_lto"
		-DVOICETYPER_OUTPUT_BASE_DIR="$output_base"
	)

	if [ "$use_cuda" = "ON" ]; then
		if [ -z "${CUDA_PATH:-}" ] && [ -d "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.2" ]; then
			export CUDA_PATH="C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.2"
		fi

		if [ -n "${CUDA_PATH:-}" ]; then
			cmake_args+=(-DCUDAToolkit_ROOT="$CUDA_PATH")
		fi
	fi

	if ! run_timed "$configure_log" "${cmake_args[@]}"; then
		COLD_CONFIGURE_MS="$MEASURED_MS"
		write_failure_row "$variant" "$backend" "$app_ipo" "$ggml_lto" "cold_configure" "$configure_log"
		return
	fi
	COLD_CONFIGURE_MS="$MEASURED_MS"

	if ! run_timed "$build_log" cmake --build "$build_dir" --config Release --parallel "$BUILD_JOBS"; then
		COLD_BUILD_MS="$MEASURED_MS"
		write_failure_row "$variant" "$backend" "$app_ipo" "$ggml_lto" "cold_build" "$build_log"
		return
	fi
	COLD_BUILD_MS="$MEASURED_MS"

	VOICE_TYPER_EXE_BYTES="$(file_size_json "$output_dir/VoiceTyper.exe")"
	BENCH_EXE_BYTES="$(file_size_json "$bench_dir/VoiceTyperBench.exe")"

	if ! run_timed "$warm_configure_log" "${cmake_args[@]}"; then
		WARM_CONFIGURE_MS="$MEASURED_MS"
		write_failure_row "$variant" "$backend" "$app_ipo" "$ggml_lto" "warm_configure" "$warm_configure_log"
		return
	fi
	WARM_CONFIGURE_MS="$MEASURED_MS"

	if ! run_timed "$warm_build_log" cmake --build "$build_dir" --config Release --parallel "$BUILD_JOBS"; then
		WARM_BUILD_MS="$MEASURED_MS"
		write_failure_row "$variant" "$backend" "$app_ipo" "$ggml_lto" "warm_build" "$warm_build_log"
		return
	fi
	WARM_BUILD_MS="$MEASURED_MS"

	if [ "$INCREMENTAL" -eq 1 ]; then
		local source_abs="$ROOT_DIR/$INCREMENTAL_SOURCE"
		local backup_file="$log_dir/incremental_source.bak"
		cp "$source_abs" "$backup_file"
		printf '\n' >> "$source_abs"

		if ! run_timed "$incremental_build_log" cmake --build "$build_dir" --config Release --parallel "$BUILD_JOBS"; then
			INCREMENTAL_BUILD_MS="$MEASURED_MS"
			cp "$backup_file" "$source_abs"
			write_failure_row "$variant" "$backend" "$app_ipo" "$ggml_lto" "incremental_build" "$incremental_build_log"
			return
		fi
		INCREMENTAL_BUILD_MS="$MEASURED_MS"
		cp "$backup_file" "$source_abs"
	fi

	local bench_exe="$bench_dir/VoiceTyperBench.exe"
	local bench_args=(
		"$bench_exe"
		--model "$MODEL_PATH"
		--audio "$AUDIO_PATH"
		--warmup "$WARMUP_COUNT"
		--iterations "$ITERATION_COUNT"
		--threads "$THREAD_COUNT"
		--cuda-device "$CUDA_DEVICE"
	)

	if [ "$HAS_EXPECTED_TEXT" -eq 1 ]; then
		bench_args+=(--expected-text "$EXPECTED_TEXT")
	fi

	local start_ms
	local end_ms
	start_ms="$(now_ms)"
	"${bench_args[@]}" > "$run_stdout" 2> "$run_stderr"
	local run_status=$?
	end_ms="$(now_ms)"
	if [ "$run_status" -ne 0 ]; then
		write_failure_row "$variant" "$backend" "$app_ipo" "$ggml_lto" "run" "$run_stderr"
		return
	fi

	local runtime_json
	runtime_json="$(tr -d '\r\n' < "$run_stdout")"
	case "$runtime_json" in
		\{*) write_success_row "$variant" "$backend" "$app_ipo" "$ggml_lto" "$runtime_json" ;;
		*) write_failure_row "$variant" "$backend" "$app_ipo" "$ggml_lto" "run_parse" "$run_stdout" ;;
	esac
}

run_variant cpu-baseline cpu OFF OFF OFF
run_variant cpu-app-ipo cpu ON OFF OFF
run_variant cpu-ggml-lto cpu OFF ON OFF
run_variant cpu-app-ipo-ggml-lto cpu ON ON OFF

if [ "$INCLUDE_CUDA" -eq 1 ]; then
	run_variant cuda-baseline cuda OFF OFF ON
	run_variant cuda-app-ipo cuda ON OFF ON
	run_variant cuda-ggml-lto cuda OFF ON ON
	run_variant cuda-app-ipo-ggml-lto cuda ON ON ON
fi

echo "Results written to $OUTPUT_PATH"
