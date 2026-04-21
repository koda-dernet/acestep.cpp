@echo off
rem -------------------------------------------------------------------------
rem pick-cuda-arch.cmd -- choose a CMAKE_CUDA_ARCHITECTURES list that the
rem installed CUDA Toolkit actually supports.
rem
rem Sets ACESTEP_CUDA_ARCH. You can override manually by setting
rem ACESTEP_CUDA_ARCH before invoking any buildXxx.cmd script.
rem
rem Arch cheat-sheet:
rem   75  Turing   (GTX 16xx, RTX 20xx, T4)
rem   80  Ampere    (A100)
rem   86  Ampere    (RTX 30xx)
rem   89  Ada       (RTX 40xx)        -- requires CUDA 11.8+
rem   90  Hopper    (H100)            -- requires CUDA 12.0+
rem   100 Blackwell (GB100)           -- requires CUDA 12.8+
rem   120a Blackwell (RTX 50xx)       -- requires CUDA 12.8+
rem   121a Blackwell Ultra            -- requires CUDA 12.9+
rem -------------------------------------------------------------------------

if defined ACESTEP_CUDA_ARCH (
  echo [pick-cuda-arch] Using user-provided ACESTEP_CUDA_ARCH=%ACESTEP_CUDA_ARCH%
  exit /b 0
)

where nvcc >nul 2>&1
if errorlevel 1 (
  echo [pick-cuda-arch] ERROR: nvcc not found on PATH.
  exit /b 1
)

rem --- Parse nvcc version ------------------------------------------------
rem "nvcc --version" emits a line like:
rem   Cuda compilation tools, release 12.8, V12.8.93
set "NVCC_VER="
for /f "tokens=5 delims=, " %%v in ('nvcc --version 2^>nul ^| findstr /C:"release"') do set "NVCC_VER=%%v"
if not defined NVCC_VER (
  echo [pick-cuda-arch] WARNING: could not parse nvcc version; using conservative default.
  set "ACESTEP_CUDA_ARCH=75-virtual;80-virtual;86-real;89-real"
  exit /b 0
)

for /f "tokens=1,2 delims=." %%a in ("%NVCC_VER%") do (
  set "NVCC_MAJOR=%%a"
  set "NVCC_MINOR=%%b"
)

rem --- Pick arch list (broadest set the installed toolkit can handle) ---
set "ACESTEP_CUDA_ARCH=75-virtual;80-virtual;86-real;89-real"
if %NVCC_MAJOR% LSS 12 goto :done
set "ACESTEP_CUDA_ARCH=75-virtual;80-virtual;86-real;89-real;90-real"
if %NVCC_MAJOR% GEQ 13 goto :full
if %NVCC_MINOR% LSS 8 goto :done
set "ACESTEP_CUDA_ARCH=75-virtual;80-virtual;86-real;89-real;90-real;120a-real"
if %NVCC_MINOR% LSS 9 goto :done
:full
set "ACESTEP_CUDA_ARCH=75-virtual;80-virtual;86-real;89-real;90-real;120a-real;121a-real"
:done

echo [pick-cuda-arch] CUDA %NVCC_MAJOR%.%NVCC_MINOR% detected; CMAKE_CUDA_ARCHITECTURES=%ACESTEP_CUDA_ARCH%
exit /b 0
