@echo off
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
cd /d "%SCRIPT_DIR%\.."

set "VARIANT=cpu"

if /I "%~1"=="cuda" (
	set "VARIANT=cuda"
) else if not "%~1"=="" (
	echo Usage: %~n0 [cuda]
	exit /b 1
)

set "EXE=build\Release_%VARIANT%\VoiceTyper.exe"

if not exist "%EXE%" (
	echo Error: "%EXE%" does not exist.
	echo Run "tools\build.bat %~1" first.
	exit /b 1
)

start "VoiceTyper" "%EXE%"
exit /b 0
