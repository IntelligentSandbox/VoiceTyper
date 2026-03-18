cmake_minimum_required(VERSION 3.16)
project(VoiceTyper VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS 
    Core 
    Gui 
    Widgets
    Multimedia
)

# whisper.cpp configuration
option(VOICETYPER_CUDA "Enable NVIDIA CUDA GPU inference" OFF)

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

set(SOURCES src/main.cpp)

qt_standard_project_setup()
qt_add_executable(${PROJECT_NAME} WIN32 ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/external/whisper.cpp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/external/whisper.cpp/ggml/include
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
    $<$<BOOL:${VOICETYPER_CUDA}>:VOICETYPER_CUDA>
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Multimedia
    whisper
)

if(VOICETYPER_CUDA)
    target_link_libraries(${PROJECT_NAME} PRIVATE ggml-cuda)
endif()

if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${Qt6_DIR}/../../../bin")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND "${WINDEPLOYQT_EXECUTABLE}" 
            --no-translations
            --no-system-d3d-compiler
            --no-opengl-sw
            $<$<CONFIG:Debug>:--debug>
            $<$<CONFIG:Release>:--release>
            "$<TARGET_FILE:${PROJECT_NAME}>"
        COMMENT "Running windeployqt"
    )
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
        set(CUDA_DLL_DIR "${CUDAToolkit_BIN_DIR}")
        if(EXISTS "${CUDAToolkit_BIN_DIR}/x64")
            set(CUDA_DLL_DIR "${CUDAToolkit_BIN_DIR}/x64")
        endif()
        set(CUDA_DLLS
            "${CUDA_DLL_DIR}/cublas64_${CUDAToolkit_VERSION_MAJOR}.dll"
            "${CUDA_DLL_DIR}/cublasLt64_${CUDAToolkit_VERSION_MAJOR}.dll"
            "${CUDA_DLL_DIR}/cudart64_${CUDAToolkit_VERSION_MAJOR}.dll"
        )
        foreach(DLL ${CUDA_DLLS})
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DLL}" "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
                COMMENT "Copying ${DLL}"
            )
        endforeach()
    endif()
endif()
