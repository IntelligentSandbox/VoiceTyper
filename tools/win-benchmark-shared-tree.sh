#!/bin/bash
# A/B benchmark for the "shared cmake tree for win cpu+cuda" backlog item.
# Compares the current flow (two separate configure+build passes, run in
# parallel as background jobs) against a unified flow (one configure pass with
# GGML_CUDA=ON + GGML_BACKEND_DL=ON, building both the VoiceTyper target and
# the ggml-cuda target from a single tree).
#
# Measures wall time for: configure + build (both targets) + assemble step.

set -u

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR" || exit 1

BUILD_JOBS="${VOICETYPER_BUILD_JOBS:-$(nproc 2>/dev/null || echo 1)}"
GENERATOR="${VOICETYPER_CMAKE_GENERATOR:-Visual Studio 17 2022}"
CUDA_PATH="${VOICETYPER_CUDA_PATH:-C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.2}"
PLATFORM_FLAG=""
case "$GENERATOR" in
	Ninja) CFG_TYPE_FLAG="-DCMAKE_BUILD_TYPE=Release" ;;
	*) PLATFORM_FLAG="-A x64"; CFG_TYPE_FLAG="" ;;
esac

now_s() { date +%s; }
ms_to_human() {
	local ms="$1"
	local s=$((ms / 1000))
	local ms_part=$((ms % 1000))
	printf "%dm%02d.%03ds" $((s / 60)) $((s % 60)) "$ms_part"
}

run_current_parallel() {
	local label="current-parallel"
	local out_base="build/shared-bench/$label-out"
	echo "=== CURRENT: parallel two-tree flow ==="
	rm -rf build/cpu build/cuda-plugin "$out_base"
	mkdir -p "$out_base"

	# Mirror what release.sh's windows_build_cpu + windows_build_cuda_plugin do,
	# minus asset staging (we only care about compile/link time).
	local cpu_bdir="build/cpu"
	local cuda_bdir="build/cuda-plugin"

	# Configure both in parallel (cheap, doesn't saturate cores).
	local cfg_start=$(now_s)
	cmake -S "$ROOT_DIR" -B "$cpu_bdir" -G "$GENERATOR" $PLATFORM_FLAG $CFG_TYPE_FLAG \
		-DVOICETYPER_BUILD_CUDA_PLUGIN=OFF \
		-DVOICETYPER_OUTPUT_BASE_DIR="$out_base" \
		> "build/shared-bench/$label-cpu-cfg.log" 2>&1 &
	local cpu_cfg_pid=$!
	cmake -S "$ROOT_DIR" -B "$cuda_bdir" -G "$GENERATOR" $PLATFORM_FLAG $CFG_TYPE_FLAG \
		-DVOICETYPER_BUILD_CUDA_PLUGIN=ON \
		-DCUDAToolkit_ROOT="$CUDA_PATH" \
		-DVOICETYPER_OUTPUT_BASE_DIR="$out_base" \
		> "build/shared-bench/$label-cuda-cfg.log" 2>&1 &
	local cuda_cfg_pid=$!
	wait $cpu_cfg_pid $cuda_cfg_pid
	local cfg_status=$?
	local cfg_ms=$(( ($(now_s) - cfg_start) * 1000 ))
	if [ "$cfg_status" -ne 0 ]; then
		echo "  CONFIGURE FAILED"
		return 1
	fi
	echo "  parallel configure: $(ms_to_human $cfg_ms)"

	# Build both in parallel (the long pole).
	local bld_start=$(now_s)
	cmake --build "$cpu_bdir" --config Release --parallel "$BUILD_JOBS" \
		> "build/shared-bench/$label-cpu-bld.log" 2>&1 &
	local cpu_bld_pid=$!
	cmake --build "$cuda_bdir" --config Release --parallel "$BUILD_JOBS" --target ggml-cuda \
		> "build/shared-bench/$label-cuda-bld.log" 2>&1 &
	local cuda_bld_pid=$!
	wait $cpu_bld_pid $cuda_bld_pid
	local bld_status=$?
	local bld_ms=$(( ($(now_s) - bld_start) * 1000 ))
	if [ "$bld_status" -ne 0 ]; then
		echo "  BUILD FAILED"
		return 1
	fi

	local total_ms=$(( ($(now_s) - cfg_start) * 1000 ))
	echo "  parallel build:     $(ms_to_human $bld_ms)"
	echo "  TOTAL (cfg+bld):    $(ms_to_human $total_ms)"

	CURRENT_CFG_MS=$cfg_ms
	CURRENT_BLD_MS=$bld_ms
	CURRENT_TOTAL_MS=$total_ms
}

run_unified_single() {
	local label="unified-single"
	local out_base="build/shared-bench/$label-out"
	echo "=== PROPOSED: unified single-tree flow ==="
	rm -rf build/unified "$out_base"
	mkdir -p "$out_base"

	local bdir="build/unified"

	local cfg_start=$(now_s)
	cmake -S "$ROOT_DIR" -B "$bdir" -G "$GENERATOR" $PLATFORM_FLAG $CFG_TYPE_FLAG \
		-DVOICETYPER_BUILD_CUDA_PLUGIN=ON \
		-DCUDAToolkit_ROOT="$CUDA_PATH" \
		-DVOICETYPER_OUTPUT_BASE_DIR="$out_base" \
		> "build/shared-bench/$label-cfg.log" 2>&1
	local cfg_status=$?
	local cfg_ms=$(( ($(now_s) - cfg_start) * 1000 ))
	if [ "$cfg_status" -ne 0 ]; then
		echo "  CONFIGURE FAILED; tail:"
		tail -20 "build/shared-bench/$label-cfg.log"
		return 1
	fi
	echo "  single configure:   $(ms_to_human $cfg_ms)"

	# Build BOTH targets in one cmake invocation. cmake's --parallel will
	# schedule cl (CPU objects) and nvcc (CUDA kernels) across the job pool.
	local bld_start=$(now_s)
	cmake --build "$bdir" --config Release --parallel "$BUILD_JOBS" \
		--target VoiceTyper ggml-cuda \
		> "build/shared-bench/$label-bld.log" 2>&1
	local bld_status=$?
	local bld_ms=$(( ($(now_s) - bld_start) * 1000 ))
	if [ "$bld_status" -ne 0 ]; then
		echo "  BUILD FAILED; tail:"
		tail -30 "build/shared-bench/$label-bld.log"
		return 1
	fi

	local total_ms=$(( ($(now_s) - cfg_start) * 1000 ))
	echo "  single build:       $(ms_to_human $bld_ms)"
	echo "  TOTAL (cfg+bld):    $(ms_to_human $total_ms)"

	UNIFIED_CFG_MS=$cfg_ms
	UNIFIED_BLD_MS=$bld_ms
	UNIFIED_TOTAL_MS=$total_ms
}

mkdir -p build/shared-bench

echo "Generator: $GENERATOR  Jobs: $BUILD_JOBS  CUDA: $CUDA_PATH"
echo ""

run_current_parallel || exit 1
echo ""
# Wipe the unified dir between runs to keep comparison fair (both face a
# populated ccache but a clean build tree).
run_unified_single || exit 1

echo ""
echo "==================== SUMMARY ===================="
printf "%-20s  configure=%s  build=%s  total=%s\n" "current-parallel" \
	"$(ms_to_human $CURRENT_CFG_MS)" "$(ms_to_human $CURRENT_BLD_MS)" "$(ms_to_human $CURRENT_TOTAL_MS)"
printf "%-20s  configure=%s  build=%s  total=%s\n" "unified-single" \
	"$(ms_to_human $UNIFIED_CFG_MS)" "$(ms_to_human $UNIFIED_BLD_MS)" "$(ms_to_human $UNIFIED_TOTAL_MS)"
echo ""
printf "configure delta: %+d ms (unified saves a pass)\n" $((CURRENT_CFG_MS - UNIFIED_CFG_MS))
printf "build delta:     %+d ms (negative = unified faster)\n" $((UNIFIED_BLD_MS - CURRENT_BLD_MS))
printf "total delta:     %+d ms (negative = unified faster)\n" $((UNIFIED_TOTAL_MS - CURRENT_TOTAL_MS))
echo "================================================"
