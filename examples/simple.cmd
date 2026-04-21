@echo off
rem Run the minimal pipeline (LM then synth) on simple.json / simple0.json.
rem Works with either generator layout (Ninja build\ or VS build\Release\).
setlocal

set "ROOT=%~dp0.."
set "BINDIR="
if exist "%ROOT%\build\ace-lm.exe"             set "BINDIR=%ROOT%\build"
if not defined BINDIR if exist "%ROOT%\build\Release\ace-lm.exe"     set "BINDIR=%ROOT%\build\Release"
if not defined BINDIR if exist "%ROOT%\build\bin\ace-lm.exe"         set "BINDIR=%ROOT%\build\bin"
if not defined BINDIR if exist "%ROOT%\build\bin\Release\ace-lm.exe" set "BINDIR=%ROOT%\build\bin\Release"

if not defined BINDIR for /f "delims=" %%F in ('dir /b /s "%ROOT%\build\ace-lm.exe" 2^>nul') do if not defined BINDIR for %%D in ("%%F") do set "BINDIR=%%~dpD"

if not defined BINDIR (
  echo [simple] ace-lm.exe not found. Build first with buildcpu / buildcuda / buildvulkan.
  pause & exit /b 1
)

set "PATH=%BINDIR%;%PATH%"
pushd "%~dp0"

ace-lm.exe ^
    --models ..\models ^
    --request simple.json
if errorlevel 1 ( popd & pause & exit /b 1 )

ace-synth.exe ^
    --models ..\models ^
    --request simple0.json

popd
pause
