#!/bin/bash
# A/B benchmark for VOICETYPER_APP_IPO on Windows. Measures clean build time
# (configure + link) and resulting exe size for IPO=ON vs IPO=OFF, then runs
# VoiceTyperBench on both to compare runtime transcription perf.
#
# Assumes a warm ccache (link is uncached, so the IPO link cost shows fully).

set -u

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR" || exit 1

BUILD_JOBS="${VOICETYPER_BUILD_JOBS:-$(nproc 2>/dev/null || echo 1)}"
GENERATOR="${VOICETYPER_CMAKE_GENERATOR:-Visual Studio 17 2022}"
PLATFORM_FLAG=""
case "$GENERATOR" in
	Ninja) ;;
	*) PLATFORM_FLAG="-A x64" ;;
esac

MODEL_PATH="${VOICETYPER_MODEL_PATH:-stt_models/ggml-base.bin}"
AUDIO_PATH="${VOICETYPER_AUDIO_PATH:-bench/jfk.wav}"
THREADS="${VOICETYPER_BENCH_THREADS:-4}"

now_s() { date +%s; }

ms_to_human() {
	local ms="$1"
	local s=$((ms / 1000))
	local ms_part=$((ms % 1000))
	printf "%dm%02d.%03ds" $((s / 60)) $((s % 60)) "$ms_part"
}

build_variant() {
	local label="$1"
	local ipo="$2"
	local bdir="build/ipo-bench/$label"
	local outbase="build/ipo-bench/out-$label"

	echo "=== $label (VOICETYPER_APP_IPO=$ipo) ==="
	rm -rf "$bdir" "$outbase"
	mkdir -p "$bdir"

	local cfg_start=$(now_s)
	cmake -S "$ROOT_DIR" -B "$bdir" -G "$GENERATOR" $PLATFORM_FLAG \
		-DVOICETYPER_BUILD_CUDA_PLUGIN=OFF \
		-DVOICETYPER_APP_IPO="$ipo" \
		-DVOICETYPER_BUILD_BENCH=ON \
		-DVOICETYPER_OUTPUT_BASE_DIR="$outbase" \
		> "$bdir/configure.log" 2>&1
	local cfg_status=$?
	local cfg_ms=$(( ($(now_s) - cfg_start) * 1000 ))
	if [ "$cfg_status" -ne 0 ]; then
		echo "  CONFIGURE FAILED; tail of log:"
		tail -20 "$bdir/configure.log"
		return 1
	fi

	local bld_start=$(now_s)
	cmake --build "$bdir" --config Release --parallel "$BUILD_JOBS" \
		> "$bdir/build.log" 2>&1
	local bld_status=$?
	local bld_ms=$(( ($(now_s) - bld_start) * 1000 ))
	if [ "$bld_status" -ne 0 ]; then
		echo "  BUILD FAILED; tail of log:"
		tail -30 "$bdir/build.log"
		return 1
	fi

	local exe="$outbase/Release_cpu/VoiceTyper.exe"
	local bench_exe="$outbase/Bench_cpu/VoiceTyperBench.exe"
	local exe_bytes=0
	local bench_bytes=0
	[ -f "$exe" ] && exe_bytes=$(stat -c%s "$exe")
	[ -f "$bench_exe" ] && bench_bytes=$(stat -c%s "$bench_exe")

	echo "  configure: $(ms_to_human $cfg_ms)  build: $(ms_to_human $bld_ms)"
	echo "  VoiceTyper.exe: $exe_bytes bytes"
	echo "  VoiceTyperBench.exe: $bench_bytes bytes"

	if [ -f "$bench_exe" ]; then
		echo "  running bench (3 warmups, 5 iters, $THREADS threads)..."
		local bench_dir
		bench_dir="$(dirname "$bench_exe")"
		local dll_src="$outbase/Release_cpu"
		# VoiceTyperBench links whisper.dll / ggml*.dll, which the build places in
		# Release_cpu/. Copy them next to the bench exe so Windows can resolve them.
		cp -u "$dll_src"/*.dll "$bench_dir"/ 2>/dev/null || true
		if (cd "$bench_dir" && ./VoiceTyperBench.exe \
			--model "$ROOT_DIR/$MODEL_PATH" \
			--audio "$ROOT_DIR/$AUDIO_PATH" \
			--warmup 3 \
			--iterations 5 \
			--threads "$THREADS") \
			> "$bdir/bench.log" 2>&1; then
			local last_line
			last_line=$(tr -d '\r' < "$bdir/bench.log" | tail -1)
			echo "  bench result: $last_line"
		else
			echo "  bench FAILED; tail:"
			tail -10 "$bdir/bench.log"
		fi
	fi

	# Stash results for the summary (label must be a valid bash var name).
	eval "${label}_CFG_MS=$cfg_ms"
	eval "${label}_BLD_MS=$bld_ms"
	eval "${label}_EXE=$exe_bytes"
	eval "${label}_BENCH_EXE=$bench_bytes"
	eval "${label}_BENCH_LOG=$bdir/bench.log"
}

summarize_bench() {
	local log="$1"
	local json_line
	# The bench prints its JSON on the last non-empty line.
	json_line=$(tr -d '\r' < "$log" | grep -E '^\{.*"transcribe_ms"' | tail -1)
	case "$json_line" in
		*\:*\"transcribe_ms\"\:\[*)
			local arr
			arr=$(printf '%s' "$json_line" | sed -n 's/.*"transcribe_ms":\[\([^]]*\)\].*/\1/p')
			if [ -z "$arr" ]; then
				printf "(no transcribe_ms)"
				return
			fi
			# Compute mean and population stddev in awk.
			printf '%s' "$arr" | awk -F, '
				{
					n = NF
					sum = 0
					for (i = 1; i <= n; i++) { xs[i] = $i; sum += $i }
					mean = sum / n
					ssq = 0
					for (i = 1; i <= n; i++) { d = xs[i] - mean; ssq += d * d }
					stddev = sqrt(ssq / n)
					printf "n=%d mean=%.1fms stddev=%.1fms", n, mean, stddev
				}'
			;;
		*)
			printf "(unparsed: %s)" "${json_line:0:120}"
			;;
	esac
}

echo "Generator: $GENERATOR  Jobs: $BUILD_JOBS  Threads: $THREADS"
echo ""

build_variant ipo_on ON || exit 1
echo ""
build_variant ipo_off OFF || exit 1

echo ""
echo "==================== SUMMARY ===================="
printf "%-12s  configure=%s  build=%s  exe=%s bytes\n" "ipo_on" \
	"$(ms_to_human $ipo_on_CFG_MS)" "$(ms_to_human $ipo_on_BLD_MS)" "$ipo_on_EXE"
printf "%-12s  configure=%s  build=%s  exe=%s bytes\n" "ipo_off" \
	"$(ms_to_human $ipo_off_CFG_MS)" "$(ms_to_human $ipo_off_BLD_MS)" "$ipo_off_EXE"
echo ""
printf "build delta (ipo_on - ipo_off): %+d ms\n" $((ipo_on_BLD_MS - ipo_off_BLD_MS))
printf "exe size delta:                 %+d bytes (%.1f%%)\n" \
	$((ipo_on_EXE - ipo_off_EXE)) \
	"$(awk "BEGIN{printf \"%.4f\", ($ipo_on_EXE - $ipo_off_EXE) / $ipo_off_EXE * 100}")"
echo ""
printf "ipo_on bench: %s\n" "$(summarize_bench "$ipo_on_BENCH_LOG")"
printf "ipo_off bench: %s\n" "$(summarize_bench "$ipo_off_BENCH_LOG")"
echo "================================================"
