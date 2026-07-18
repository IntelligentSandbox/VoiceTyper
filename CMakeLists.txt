cmake_minimum_required(VERSION 3.16)

file(READ "${CMAKE_SOURCE_DIR}/VERSION" VOICETYPER_VERSION)
string(STRIP "${VOICETYPER_VERSION}" VOICETYPER_VERSION)

project(VoiceTyper VERSION ${VOICETYPER_VERSION} LANGUAGES C CXX)

# Build a full version string. The VERSION file holds the version currently
# being developed. If HEAD is exactly at the v$VERSION tag, this is a clean
# release build and the full version is just $VERSION. Otherwise it is a dev
# build and the short SHA is appended: "$VERSION-dev-<sha>".
# Examples with VERSION=0.1.8:
#   HEAD == v0.1.8 tag  -> "0.1.8"
#   HEAD past v0.1.8    -> "0.1.8-dev-c893b49"
#   v0.1.8 tag missing  -> "0.1.8-dev-c893b49"
#   git unavailable     -> "0.1.8"
set(VOICETYPER_VERSION_FULL "${VOICETYPER_VERSION}")

find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
	execute_process(
		COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
		WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
		OUTPUT_VARIABLE _git_sha
		RESULT_VARIABLE _sha_result
		OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_QUIET
	)
	execute_process(
		COMMAND ${GIT_EXECUTABLE} tag --points-at HEAD --list "v${VOICETYPER_VERSION}"
		WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
		OUTPUT_VARIABLE _head_tag
		RESULT_VARIABLE _tag_result
		OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_QUIET
	)
	if(_sha_result EQUAL 0 AND _git_sha AND NOT _head_tag)
		set(VOICETYPER_VERSION_FULL "${VOICETYPER_VERSION}-dev-${_git_sha}")
	endif()
endif()

message(STATUS "VoiceTyper version: ${VOICETYPER_VERSION} (full: ${VOICETYPER_VERSION_FULL})")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Build options
# ---------------------------------------------------------------------------
option(VOICETYPER_CUDA "Enable NVIDIA CUDA GPU inference" OFF)
option(VOICETYPER_APP_IPO "Enable IPO for VoiceTyper application targets" ON)
option(VOICETYPER_STREAMING_WHISPER_VAD "Enable Whisper VAD for silence-bounded streaming chunks" ON)
option(VOICETYPER_RECORD_WHISPER_VAD "Enable Whisper VAD for record-mode chunks" ON)
set(GGML_LTO OFF CACHE BOOL "Enable GGML link time optimization")
set(VOICETYPER_APP_UPDATE_HZ "100" CACHE STRING "Fixed app-state update rate in Hz")
set(VOICETYPER_APP_UPDATE_MAX_CATCH_UP_TICKS "5" CACHE STRING "Maximum fixed app ticks to process per loop")
set(VOICETYPER_OUTPUT_BASE_DIR "${CMAKE_SOURCE_DIR}/build" CACHE PATH "Base directory for application outputs")

if(NOT VOICETYPER_APP_UPDATE_HZ MATCHES "^[1-9][0-9]*$")
	message(FATAL_ERROR "VOICETYPER_APP_UPDATE_HZ must be a positive integer")
endif()

if(NOT VOICETYPER_APP_UPDATE_MAX_CATCH_UP_TICKS MATCHES "^[1-9][0-9]*$")
	message(FATAL_ERROR "VOICETYPER_APP_UPDATE_MAX_CATCH_UP_TICKS must be a positive integer")
endif()

# ---------------------------------------------------------------------------
# whisper.cpp configuration
# ---------------------------------------------------------------------------
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(GGML_METAL OFF CACHE BOOL "" FORCE)
set(GGML_VULKAN OFF CACHE BOOL "" FORCE)
set(GGML_SYCL OFF CACHE BOOL "" FORCE)

if(VOICETYPER_CUDA)
	set(GGML_CUDA ON CACHE BOOL "" FORCE)
else()
	set(GGML_CUDA OFF CACHE BOOL "" FORCE)
endif()

if(MSVC)
	add_compile_options(
		$<$<COMPILE_LANGUAGE:C>:/MP>
		$<$<COMPILE_LANGUAGE:CXX>:/MP>
	)
endif()

if(VOICETYPER_APP_IPO)
	include(CheckIPOSupported)
	check_ipo_supported(RESULT VOICETYPER_IPO_SUPPORTED OUTPUT VOICETYPER_IPO_ERROR)
	if(NOT VOICETYPER_IPO_SUPPORTED)
		message(FATAL_ERROR "VOICETYPER_APP_IPO is not supported: ${VOICETYPER_IPO_ERROR}")
	endif()
endif()

add_subdirectory(src/external/whisper.cpp)

# ---------------------------------------------------------------------------
# ImGui build
# ---------------------------------------------------------------------------
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/external/imgui-1.92.6)

set(IMGUI_SOURCES
	${IMGUI_DIR}/imgui.cpp
	${IMGUI_DIR}/imgui_draw.cpp
	${IMGUI_DIR}/imgui_tables.cpp
	${IMGUI_DIR}/imgui_widgets.cpp
	${IMGUI_DIR}/backends/imgui_impl_win32.cpp
	${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)

set(SOURCES
	src/imgui_main.cpp
	${IMGUI_SOURCES}
)

if(WIN32)
	set(APP_ICON_RESOURCE "${CMAKE_SOURCE_DIR}/src/resources.rc")
endif()

if(VOICETYPER_CUDA)
	set(VARIANT_SUFFIX "_cuda")
else()
	set(VARIANT_SUFFIX "_cpu")
endif()

set(VOICETYPER_RELEASE_OUTPUT_DIR "${VOICETYPER_OUTPUT_BASE_DIR}/Release${VARIANT_SUFFIX}")
set(VOICETYPER_DEBUG_OUTPUT_DIR "${VOICETYPER_OUTPUT_BASE_DIR}/Debug${VARIANT_SUFFIX}")
set(VOICETYPER_BENCH_OUTPUT_DIR "${VOICETYPER_OUTPUT_BASE_DIR}/Bench${VARIANT_SUFFIX}")

function(apply_voicetyper_target_options TARGET_NAME)
	set_target_properties(${TARGET_NAME} PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY_RELEASE "${VOICETYPER_RELEASE_OUTPUT_DIR}"
		RUNTIME_OUTPUT_DIRECTORY_DEBUG "${VOICETYPER_DEBUG_OUTPUT_DIR}"
	)

	if(VOICETYPER_APP_IPO)
		set_target_properties(${TARGET_NAME} PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
	endif()
endfunction()

function(apply_voicetyper_bench_target_options TARGET_NAME)
	set_target_properties(${TARGET_NAME} PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY_RELEASE "${VOICETYPER_BENCH_OUTPUT_DIR}"
		RUNTIME_OUTPUT_DIRECTORY_DEBUG "${VOICETYPER_DEBUG_OUTPUT_DIR}"
	)
endfunction()

add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${APP_ICON_RESOURCE})
apply_voicetyper_target_options(${PROJECT_NAME})

target_include_directories(${PROJECT_NAME} PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/src/external/whisper.cpp/include
	${CMAKE_CURRENT_SOURCE_DIR}/src/external/whisper.cpp/ggml/include
	${IMGUI_DIR}
	${IMGUI_DIR}/backends
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
	$<$<CONFIG:Debug>:DEBUG>
	$<$<BOOL:${VOICETYPER_CUDA}>:VOICETYPER_CUDA>
	VOICETYPER_STREAMING_WHISPER_VAD=$<BOOL:${VOICETYPER_STREAMING_WHISPER_VAD}>
	VOICETYPER_RECORD_WHISPER_VAD=$<BOOL:${VOICETYPER_RECORD_WHISPER_VAD}>
	VOICETYPER_APP_UPDATE_HZ=${VOICETYPER_APP_UPDATE_HZ}
	VOICETYPER_APP_UPDATE_MAX_CATCH_UP_TICKS=${VOICETYPER_APP_UPDATE_MAX_CATCH_UP_TICKS}
	VOICETYPER_VERSION_FULL="${VOICETYPER_VERSION_FULL}"
)

target_link_libraries(${PROJECT_NAME} PRIVATE
	whisper
	d3d11
	dxgi
	d3dcompiler
)

if(VOICETYPER_CUDA)
	target_link_libraries(${PROJECT_NAME} PRIVATE ggml-cuda)
endif()

add_executable(VoiceTyperBench src/benchmark_main.cpp)
apply_voicetyper_bench_target_options(VoiceTyperBench)

target_include_directories(VoiceTyperBench PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/src
	${CMAKE_CURRENT_SOURCE_DIR}/src/external/whisper.cpp/include
	${CMAKE_CURRENT_SOURCE_DIR}/src/external/whisper.cpp/ggml/include
)

target_compile_definitions(VoiceTyperBench PRIVATE
	$<$<CONFIG:Debug>:DEBUG>
	$<$<BOOL:${VOICETYPER_CUDA}>:VOICETYPER_CUDA>
	VOICETYPER_VERSION_FULL="${VOICETYPER_VERSION_FULL}"
)

target_link_libraries(VoiceTyperBench PRIVATE whisper)

if(VOICETYPER_CUDA)
	target_link_libraries(VoiceTyperBench PRIVATE ggml-cuda)
endif()

if(WIN32 AND VOICETYPER_CUDA)
	find_package(CUDAToolkit REQUIRED)
endif()
