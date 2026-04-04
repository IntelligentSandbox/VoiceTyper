cmake_minimum_required(VERSION 3.16)

file(READ "${CMAKE_SOURCE_DIR}/VERSION" VOICETYPER_VERSION)
string(STRIP "${VOICETYPER_VERSION}" VOICETYPER_VERSION)

project(VoiceTyper VERSION ${VOICETYPER_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Build options
# ---------------------------------------------------------------------------
option(VOICETYPER_CUDA "Enable NVIDIA CUDA GPU inference" OFF)

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

add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${APP_ICON_RESOURCE})

set_target_properties(${PROJECT_NAME} PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/build/Release${VARIANT_SUFFIX}"
	RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/build/Debug${VARIANT_SUFFIX}"
)

target_include_directories(${PROJECT_NAME} PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/src/external/whisper.cpp/include
	${CMAKE_CURRENT_SOURCE_DIR}/src/external/whisper.cpp/ggml/include
	${IMGUI_DIR}
	${IMGUI_DIR}/backends
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
	$<$<CONFIG:Debug>:DEBUG>
	$<$<BOOL:${VOICETYPER_CUDA}>:VOICETYPER_CUDA>
	VOICETYPER_VERSION="${VOICETYPER_VERSION}"
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

if(WIN32)
	add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND cmd /c robocopy
			"${CMAKE_SOURCE_DIR}\\models"
			"$<TARGET_FILE_DIR:${PROJECT_NAME}>\\models"
			/E /NJH /NJS /NFL /NDL
			"||" exit 0
		COMMENT "Copying models to output directory"
	)
	add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND cmd /c robocopy
			"${CMAKE_SOURCE_DIR}\\media"
			"$<TARGET_FILE_DIR:${PROJECT_NAME}>\\media"
			/E /NJH /NJS /NFL /NDL
			"||" exit 0
		COMMENT "Copying media to output directory"
	)
	if(VOICETYPER_CUDA)
		find_package(CUDAToolkit REQUIRED)
	endif()
endif()
