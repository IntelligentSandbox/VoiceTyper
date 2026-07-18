@echo off
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
set "TARGET_SCRIPT=%SCRIPT_DIR%\release.sh"
set "GIT_BASH="

if not exist "%TARGET_SCRIPT%" goto missing_script
if exist "%ProgramFiles%\Git\bin\bash.exe" set "GIT_BASH=%ProgramFiles%\Git\bin\bash.exe"
if not defined GIT_BASH if exist "%ProgramFiles(x86)%\Git\bin\bash.exe" set "GIT_BASH=%ProgramFiles(x86)%\Git\bin\bash.exe"
if defined GIT_BASH goto run_script

for /f "delims=" %%B in ('where bash.exe 2^>nul') do (
	set "GIT_BASH=%%B"
	goto run_script
)

echo Error: Git Bash was not found.
echo Install Git for Windows or add bash.exe to PATH.
exit /b 1

:missing_script
echo Error: release script was not found at "%TARGET_SCRIPT%".
exit /b 1

:run_script
cd /d "%SCRIPT_DIR%\.."
"%GIT_BASH%" "%TARGET_SCRIPT%" %*
exit /b %ERRORLEVEL%
