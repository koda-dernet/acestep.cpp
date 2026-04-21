@echo off
rem GPU build with CUDA backend. Requires CUDA Toolkit installed.
setlocal

call "%~dp0setup-vcvars.cmd"
if errorlevel 1 exit /b 1

rem --- Sanity-check CUDA is available ---
where nvcc >nul 2>&1
if errorlevel 1 (
  echo [buildcuda] ERROR: nvcc not found.
  echo   Install the CUDA Toolkit from https://developer.nvidia.com/cuda-downloads
  echo   and make sure CUDA_PATH is set ^(the installer does this by default^).
  exit /b 1
)

rem --- If using the Visual Studio generator, CUDA also needs its MSBuild ---
rem --- integration files. Switch to Ninja silently if those are missing.  ---
if /i "%ACESTEP_GEN%"=="Visual Studio 17 2022" (
  if not exist "%VSINSTALL%\MSBuild\Microsoft\VC\v170\BuildCustomizations\CUDA*.targets" (
    echo [buildcuda] VS CUDA integration not found; falling back to Ninja.
    echo   ^(Install it from CUDA's "Visual Studio Integration" component, or ignore.^)
    set "ACESTEP_GEN=Ninja"
  )
)

set "ACESTEP_CMAKE_FLAGS=-DGGML_CUDA=ON"
call "%~dp0build-common.cmd"
exit /b %ERRORLEVEL%
