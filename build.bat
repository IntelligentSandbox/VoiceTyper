@echo off
setlocal EnableExtensions

set "BUILD_TYPE=Release"
set "USE_CUDA=OFF"

:parse_args
if "%~1"=="" goto args_done
if /I "%~1"=="debug" set "BUILD_TYPE=Debug"
if /I "%~1"=="cuda" set "USE_CUDA=ON"
shift
goto parse_args

:args_done
if /I "%USE_CUDA%"=="ON" (
	set "BUILD_DIR=build\cuda"
	set "VARIANT=cuda"
) else (
	set "BUILD_DIR=build\cpu"
	set "VARIANT=cpu"
)

set "OUTPUT_DIR=build\%BUILD_TYPE%_%VARIANT%"
set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

if /I "%USE_CUDA%"=="ON" (
	set "CUDA_PATH=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.2"
)

pushd "%BUILD_DIR%"
if /I "%USE_CUDA%"=="ON" (
	cmake ..\.. -G "Visual Studio 17 2022" -A x64 -DVOICETYPER_CUDA=ON -DCUDAToolkit_ROOT="%CUDA_PATH%"
) else (
	cmake ..\.. -G "Visual Studio 17 2022" -A x64 -DVOICETYPER_CUDA=OFF
)
if errorlevel 1 goto fail
cmake --build . --config "%BUILD_TYPE%"
if errorlevel 1 goto fail_popd
popd

if exist "%OUTPUT_DIR%\stt_models" rmdir /S /Q "%OUTPUT_DIR%\stt_models"
if exist "%OUTPUT_DIR%\vad_models" rmdir /S /Q "%OUTPUT_DIR%\vad_models"
if exist "%OUTPUT_DIR%\media" rmdir /S /Q "%OUTPUT_DIR%\media"
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"

xcopy /E /I /Y "stt_models" "%OUTPUT_DIR%\stt_models" >nul
if errorlevel 1 goto fail
xcopy /E /I /Y "vad_models" "%OUTPUT_DIR%\vad_models" >nul
if errorlevel 1 goto fail
xcopy /E /I /Y "media" "%OUTPUT_DIR%\media" >nul
if errorlevel 1 goto fail

if not exist "%OUTPUT_DIR%\data" mkdir "%OUTPUT_DIR%\data"
if not exist "%OUTPUT_DIR%\data\settings.ini" type nul > "%OUTPUT_DIR%\data\settings.ini"

if /I "%USE_CUDA%"=="ON" (
	set "CUDA_DLL_DIR=%CUDA_PATH%\bin"
	if exist "%CUDA_PATH%\bin\x64" set "CUDA_DLL_DIR=%CUDA_PATH%\bin\x64"
	for %%F in ("%CUDA_DLL_DIR%\cublas64_*.dll") do copy /Y "%%~fF" "%SCRIPT_DIR%\%OUTPUT_DIR%\" >nul
	for %%F in ("%CUDA_DLL_DIR%\cublasLt64_*.dll") do copy /Y "%%~fF" "%SCRIPT_DIR%\%OUTPUT_DIR%\" >nul
)

exit /b 0

:fail_popd
popd

:fail
exit /b 1
