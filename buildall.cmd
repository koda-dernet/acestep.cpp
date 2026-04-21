@echo off
rem Everything: CPU variants + CUDA + Vulkan, with backend DL so backends
rem load at runtime. Requires CUDA Toolkit and Vulkan SDK both installed.
setlocal

call "%~dp0setup-vcvars.cmd"
if errorlevel 1 exit /b 1

where nvcc >nul 2>&1
if errorlevel 1 (
  echo [buildall] ERROR: nvcc not found. Install the CUDA Toolkit.
  exit /b 1
)
if not defined VULKAN_SDK (
  echo [buildall] ERROR: VULKAN_SDK is not set. Install the LunarG Vulkan SDK.
  exit /b 1
)

if /i "%ACESTEP_GEN%"=="Visual Studio 17 2022" (
  if not exist "%VSINSTALL%\MSBuild\Microsoft\VC\v170\BuildCustomizations\CUDA*.targets" (
    echo [buildall] VS CUDA integration not found; falling back to Ninja.
    set "ACESTEP_GEN=Ninja"
  )
)

set "ACESTEP_CMAKE_FLAGS=-DGGML_CPU_ALL_VARIANTS=ON -DGGML_CUDA=ON -DGGML_VULKAN=ON -DGGML_BACKEND_DL=ON"
call "%~dp0build-common.cmd"
exit /b %ERRORLEVEL%
