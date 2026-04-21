@echo off
rem Run ace-server from the build output. Works with either generator layout:
rem   Ninja           -> build\ace-server.exe
rem   VS 2022 gen.    -> build\Release\ace-server.exe
setlocal

set "EXE="
if exist "%~dp0build\ace-server.exe"             set "EXE=%~dp0build\ace-server.exe"
if not defined EXE if exist "%~dp0build\Release\ace-server.exe"     set "EXE=%~dp0build\Release\ace-server.exe"
if not defined EXE if exist "%~dp0build\bin\ace-server.exe"         set "EXE=%~dp0build\bin\ace-server.exe"
if not defined EXE if exist "%~dp0build\bin\Release\ace-server.exe" set "EXE=%~dp0build\bin\Release\ace-server.exe"

rem Final fallback: scan the build tree (slower, but never misses).
if not defined EXE for /f "delims=" %%F in ('dir /b /s "%~dp0build\ace-server.exe" 2^>nul') do if not defined EXE set "EXE=%%F"

if not defined EXE (
  echo [server] ace-server.exe not found. Build first with one of:
  echo   buildcpu.cmd       ^(CPU-only^)
  echo   buildcuda.cmd      ^(CUDA^)
  echo   buildvulkan.cmd    ^(Vulkan^)
  echo   buildall.cmd       ^(everything^)
  pause
  exit /b 1
)

rem Prepend the build folder so dependent DLLs (backends, ggml) are found.
for %%F in ("%EXE%") do set "EXEDIR=%%~dpF"
set "PATH=%EXEDIR%;%PATH%"

rem Create default model / adapter folders so first-run doesn't error out.
if not exist "%~dp0models"   mkdir "%~dp0models"
if not exist "%~dp0adapters" mkdir "%~dp0adapters"

rem Multi-GPU: set GGML_BACKEND to pick a device (CUDA0, CUDA1, Vulkan0 ...)
rem set GGML_BACKEND=CUDA0
rem set GGML_BACKEND=Vulkan0

"%EXE%" ^
    --host 0.0.0.0 ^
    --port 8085 ^
    --models "%~dp0models" ^
    --adapters "%~dp0adapters" ^
    --max-batch 1

pause
