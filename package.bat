@echo off
setlocal EnableExtensions

set "REBUILD=0"

:parse_args
if "%~1"=="" goto args_done
if /I not "%~1"=="build" goto usage
set "REBUILD=1"
shift
goto parse_args

:usage
echo Usage: package.bat [build]
exit /b 1

:args_done
set "VERSION="
for /f "usebackq delims=" %%V in ("VERSION") do (
	set "VERSION=%%V"
	goto version_done
)

:version_done
set "VERSION=%VERSION: =%"
if not "%VERSION%"=="" goto version_ok
echo Error: VERSION is empty or missing.
exit /b 1

:version_ok
set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"

set "DIST_DIR=dist"
set "PACKAGE_PLATFORM=x64_win"
set "CPU_BUILD=build\Release_cpu"
set "CUDA_BUILD=build\Release_cuda"
set "STAGE_DIR=build\package_%PACKAGE_PLATFORM%"
set "CPU_MODELS_STAGE=%STAGE_DIR%\cpu-base-en-silero"
set "CUDA_NO_MODELS_STAGE=%STAGE_DIR%\cuda-no-models"
set "CUDA_MODELS_STAGE=%STAGE_DIR%\cuda-base-en-silero"
set "JOB_DIR=%STAGE_DIR%\jobs"
set "CPU_EXE_DIST=%DIST_DIR%\VoiceTyper-v%VERSION%-%PACKAGE_PLATFORM%-cpu.exe"
set "CPU_MODELS_ZIP=%DIST_DIR%\voicetyper-v%VERSION%-%PACKAGE_PLATFORM%-cpu-base-en-silero.zip"
set "CUDA_NO_MODELS_ZIP=%DIST_DIR%\voicetyper-v%VERSION%-%PACKAGE_PLATFORM%-cuda-no-models.zip"
set "CUDA_MODELS_ZIP=%DIST_DIR%\voicetyper-v%VERSION%-%PACKAGE_PLATFORM%-cuda-base-en-silero.zip"
set "CPU_MSI=%DIST_DIR%\voicetyper-v%VERSION%-%PACKAGE_PLATFORM%-cpu.msi"
set "CUDA_MSI=%DIST_DIR%\voicetyper-v%VERSION%-%PACKAGE_PLATFORM%-cuda.msi"

if "%REBUILD%"=="1" goto clean_builds
if not exist "%CUDA_BUILD%\VoiceTyper.exe" goto missing_cuda
if not exist "%CPU_BUILD%\VoiceTyper.exe" goto missing_cpu
goto prepare_dirs

:clean_builds
if exist "%CUDA_BUILD%" rmdir /S /Q "%CUDA_BUILD%"
if exist "%CPU_BUILD%" rmdir /S /Q "%CPU_BUILD%"

:prepare_dirs
if exist "%DIST_DIR%" rmdir /S /Q "%DIST_DIR%"
if exist "%STAGE_DIR%" rmdir /S /Q "%STAGE_DIR%"
mkdir "%DIST_DIR%"
if errorlevel 1 goto fail
mkdir "%STAGE_DIR%"
if errorlevel 1 goto fail

if not "%REBUILD%"=="1" goto skip_cuda_build
echo.
echo === Building Release (CUDA) ===
call build.bat cuda
if errorlevel 1 goto fail

:skip_cuda_build
if not "%REBUILD%"=="1" goto skip_cpu_build
echo.
echo === Building Release (CPU) ===
call build.bat
if errorlevel 1 goto fail

:skip_cpu_build
echo.
echo === Staging package inputs (%PACKAGE_PLATFORM%) ===
xcopy /E /I /Y "%CUDA_BUILD%" "%CUDA_NO_MODELS_STAGE%" >nul
if errorlevel 1 goto fail
if exist "%CUDA_NO_MODELS_STAGE%\stt_models" rmdir /S /Q "%CUDA_NO_MODELS_STAGE%\stt_models"
if exist "%CUDA_NO_MODELS_STAGE%\vad_models" rmdir /S /Q "%CUDA_NO_MODELS_STAGE%\vad_models"

xcopy /E /I /Y "%CUDA_BUILD%" "%CUDA_MODELS_STAGE%" >nul
if errorlevel 1 goto fail
if exist "%CUDA_MODELS_STAGE%\stt_models" rmdir /S /Q "%CUDA_MODELS_STAGE%\stt_models"
mkdir "%CUDA_MODELS_STAGE%\stt_models"
if errorlevel 1 goto fail
copy /Y "stt_models\ggml-base.en.bin" "%CUDA_MODELS_STAGE%\stt_models\" >nul
if errorlevel 1 goto fail
if exist "%CUDA_MODELS_STAGE%\vad_models" rmdir /S /Q "%CUDA_MODELS_STAGE%\vad_models"
mkdir "%CUDA_MODELS_STAGE%\vad_models"
if errorlevel 1 goto fail
copy /Y "vad_models\ggml-silero-v5.1.2.bin" "%CUDA_MODELS_STAGE%\vad_models\" >nul
if errorlevel 1 goto fail

xcopy /E /I /Y "%CPU_BUILD%" "%CPU_MODELS_STAGE%" >nul
if errorlevel 1 goto fail
if exist "%CPU_MODELS_STAGE%\stt_models" rmdir /S /Q "%CPU_MODELS_STAGE%\stt_models"
mkdir "%CPU_MODELS_STAGE%\stt_models"
if errorlevel 1 goto fail
copy /Y "stt_models\ggml-base.en.bin" "%CPU_MODELS_STAGE%\stt_models\" >nul
if errorlevel 1 goto fail
if exist "%CPU_MODELS_STAGE%\vad_models" rmdir /S /Q "%CPU_MODELS_STAGE%\vad_models"
mkdir "%CPU_MODELS_STAGE%\vad_models"
if errorlevel 1 goto fail
copy /Y "vad_models\ggml-silero-v5.1.2.bin" "%CPU_MODELS_STAGE%\vad_models\" >nul
if errorlevel 1 goto fail

copy /Y "%CPU_BUILD%\VoiceTyper.exe" "%CPU_EXE_DIST%" >nul
if errorlevel 1 goto fail
mkdir "%JOB_DIR%"
if errorlevel 1 goto fail

> "%JOB_DIR%\cuda_no_zip.bat" echo @echo off
>> "%JOB_DIR%\cuda_no_zip.bat" echo cd /d "%SCRIPT_DIR%\%CUDA_NO_MODELS_STAGE%"
>> "%JOB_DIR%\cuda_no_zip.bat" echo 7z a -tzip "%SCRIPT_DIR%\%CUDA_NO_MODELS_ZIP%" -r . ^>nul
>> "%JOB_DIR%\cuda_no_zip.bat" echo if errorlevel 1 goto fail
>> "%JOB_DIR%\cuda_no_zip.bat" echo echo 0 ^> "%SCRIPT_DIR%\%JOB_DIR%\cuda_no_zip.exit"
>> "%JOB_DIR%\cuda_no_zip.bat" echo exit /b 0
>> "%JOB_DIR%\cuda_no_zip.bat" echo :fail
>> "%JOB_DIR%\cuda_no_zip.bat" echo echo 1 ^> "%SCRIPT_DIR%\%JOB_DIR%\cuda_no_zip.exit"
>> "%JOB_DIR%\cuda_no_zip.bat" echo exit /b 1

> "%JOB_DIR%\cuda_models_zip.bat" echo @echo off
>> "%JOB_DIR%\cuda_models_zip.bat" echo cd /d "%SCRIPT_DIR%\%CUDA_MODELS_STAGE%"
>> "%JOB_DIR%\cuda_models_zip.bat" echo 7z a -tzip "%SCRIPT_DIR%\%CUDA_MODELS_ZIP%" -r . ^>nul
>> "%JOB_DIR%\cuda_models_zip.bat" echo if errorlevel 1 goto fail
>> "%JOB_DIR%\cuda_models_zip.bat" echo echo 0 ^> "%SCRIPT_DIR%\%JOB_DIR%\cuda_models_zip.exit"
>> "%JOB_DIR%\cuda_models_zip.bat" echo exit /b 0
>> "%JOB_DIR%\cuda_models_zip.bat" echo :fail
>> "%JOB_DIR%\cuda_models_zip.bat" echo echo 1 ^> "%SCRIPT_DIR%\%JOB_DIR%\cuda_models_zip.exit"
>> "%JOB_DIR%\cuda_models_zip.bat" echo exit /b 1

> "%JOB_DIR%\cuda_msi.bat" echo @echo off
>> "%JOB_DIR%\cuda_msi.bat" echo cd /d "%SCRIPT_DIR%"
>> "%JOB_DIR%\cuda_msi.bat" echo wix build -o "%CUDA_MSI%" -pdbtype none ^^
>> "%JOB_DIR%\cuda_msi.bat" echo 	-d "BuildOutput=%SCRIPT_DIR%\%CUDA_MODELS_STAGE%" ^^
>> "%JOB_DIR%\cuda_msi.bat" echo 	-d "ProductVersion=%VERSION%" ^^
>> "%JOB_DIR%\cuda_msi.bat" echo 	packaging\VoiceTyper.wxs
>> "%JOB_DIR%\cuda_msi.bat" echo if errorlevel 1 goto fail
>> "%JOB_DIR%\cuda_msi.bat" echo echo 0 ^> "%JOB_DIR%\cuda_msi.exit"
>> "%JOB_DIR%\cuda_msi.bat" echo exit /b 0
>> "%JOB_DIR%\cuda_msi.bat" echo :fail
>> "%JOB_DIR%\cuda_msi.bat" echo echo 1 ^> "%JOB_DIR%\cuda_msi.exit"
>> "%JOB_DIR%\cuda_msi.bat" echo exit /b 1

> "%JOB_DIR%\cpu_models_zip.bat" echo @echo off
>> "%JOB_DIR%\cpu_models_zip.bat" echo cd /d "%SCRIPT_DIR%\%CPU_MODELS_STAGE%"
>> "%JOB_DIR%\cpu_models_zip.bat" echo 7z a -tzip "%SCRIPT_DIR%\%CPU_MODELS_ZIP%" -r . ^>nul
>> "%JOB_DIR%\cpu_models_zip.bat" echo if errorlevel 1 goto fail
>> "%JOB_DIR%\cpu_models_zip.bat" echo echo 0 ^> "%SCRIPT_DIR%\%JOB_DIR%\cpu_models_zip.exit"
>> "%JOB_DIR%\cpu_models_zip.bat" echo exit /b 0
>> "%JOB_DIR%\cpu_models_zip.bat" echo :fail
>> "%JOB_DIR%\cpu_models_zip.bat" echo echo 1 ^> "%SCRIPT_DIR%\%JOB_DIR%\cpu_models_zip.exit"
>> "%JOB_DIR%\cpu_models_zip.bat" echo exit /b 1

> "%JOB_DIR%\cpu_msi.bat" echo @echo off
>> "%JOB_DIR%\cpu_msi.bat" echo cd /d "%SCRIPT_DIR%"
>> "%JOB_DIR%\cpu_msi.bat" echo wix build -o "%CPU_MSI%" -pdbtype none ^^
>> "%JOB_DIR%\cpu_msi.bat" echo 	-d "BuildOutput=%SCRIPT_DIR%\%CPU_MODELS_STAGE%" ^^
>> "%JOB_DIR%\cpu_msi.bat" echo 	-d "ProductVersion=%VERSION%" ^^
>> "%JOB_DIR%\cpu_msi.bat" echo 	packaging\VoiceTyper.wxs
>> "%JOB_DIR%\cpu_msi.bat" echo if errorlevel 1 goto fail
>> "%JOB_DIR%\cpu_msi.bat" echo echo 0 ^> "%JOB_DIR%\cpu_msi.exit"
>> "%JOB_DIR%\cpu_msi.bat" echo exit /b 0
>> "%JOB_DIR%\cpu_msi.bat" echo :fail
>> "%JOB_DIR%\cpu_msi.bat" echo echo 1 ^> "%JOB_DIR%\cpu_msi.exit"
>> "%JOB_DIR%\cpu_msi.bat" echo exit /b 1

echo.
echo === Creating package artifacts (%PACKAGE_PLATFORM%) ===
echo === Starting CUDA no-models zip ===
start "" /B cmd /C call "%JOB_DIR%\cuda_no_zip.bat"
echo === Starting CUDA base.en + silero zip ===
start "" /B cmd /C call "%JOB_DIR%\cuda_models_zip.bat"
echo === Starting CUDA MSI ===
start "" /B cmd /C call "%JOB_DIR%\cuda_msi.bat"
echo === Starting CPU base.en + silero zip ===
start "" /B cmd /C call "%JOB_DIR%\cpu_models_zip.bat"
echo === Starting CPU MSI ===
start "" /B cmd /C call "%JOB_DIR%\cpu_msi.bat"

:wait_jobs
if not exist "%JOB_DIR%\cuda_no_zip.exit" goto wait_sleep
if not exist "%JOB_DIR%\cuda_models_zip.exit" goto wait_sleep
if not exist "%JOB_DIR%\cuda_msi.exit" goto wait_sleep
if not exist "%JOB_DIR%\cpu_models_zip.exit" goto wait_sleep
if not exist "%JOB_DIR%\cpu_msi.exit" goto wait_sleep
goto check_jobs

:wait_sleep
ping -n 2 127.0.0.1 >nul
goto wait_jobs

:check_jobs
set "FAILED=0"
findstr /B /C:"0" "%JOB_DIR%\cuda_no_zip.exit" >nul
if errorlevel 1 set "FAILED=1"
findstr /B /C:"0" "%JOB_DIR%\cuda_models_zip.exit" >nul
if errorlevel 1 set "FAILED=1"
findstr /B /C:"0" "%JOB_DIR%\cuda_msi.exit" >nul
if errorlevel 1 set "FAILED=1"
findstr /B /C:"0" "%JOB_DIR%\cpu_models_zip.exit" >nul
if errorlevel 1 set "FAILED=1"
findstr /B /C:"0" "%JOB_DIR%\cpu_msi.exit" >nul
if errorlevel 1 set "FAILED=1"
if "%FAILED%"=="1" goto package_fail

echo === Finished package artifacts (%PACKAGE_PLATFORM%) ===
echo.
echo === Done ===
echo Created:
dir /B "%DIST_DIR%"
exit /b 0

:package_fail
echo Error: one or more package artifact jobs failed.
goto fail

:missing_cuda
echo Error: cuda build output "%CUDA_BUILD%" does not exist.
echo Run "build.bat cuda" first, or run "package.bat build".
exit /b 1

:missing_cpu
echo Error: cpu build output "%CPU_BUILD%" does not exist.
echo Run "build.bat" first, or run "package.bat build".
exit /b 1

:fail
exit /b 1
