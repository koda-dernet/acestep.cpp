@echo off
rem -------------------------------------------------------------------------
rem build-common.cmd -- internal helper sourced by buildcpu/cuda/vulkan/all
rem
rem Prereq: setup-vcvars.cmd has already been called (ACESTEP_GEN is set).
rem Uses %ACESTEP_CMAKE_FLAGS% for the configure-time flags, e.g.
rem   set "ACESTEP_CMAKE_FLAGS=-DGGML_CUDA=ON"
rem -------------------------------------------------------------------------

rem --- 1. Create / repair build dir -------------------------------------
mkdir build 2>nul

rem Detect generator change and drop the stale cache. This keeps object
rem files from previous compatible builds, while forcing a re-configure
rem when you switch between Ninja and the VS generator.
if exist build\CMakeCache.txt (
  findstr /c:"CMAKE_GENERATOR:INTERNAL=%ACESTEP_GEN%" build\CMakeCache.txt >nul 2>&1
  if errorlevel 1 (
    echo [build] Generator change detected; removing stale CMake cache.
    del /q build\CMakeCache.txt
    if exist build\CMakeFiles rd /s /q build\CMakeFiles
  )
)

rem --- 2. Configure ------------------------------------------------------
pushd build
if /i "%ACESTEP_GEN%"=="Ninja" (
  cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release %ACESTEP_CMAKE_FLAGS%
) else (
  cmake .. -G "Visual Studio 17 2022" -A x64 %ACESTEP_CMAKE_FLAGS%
)
if errorlevel 1 ( popd & exit /b 1 )

rem --- 3. Build ----------------------------------------------------------
if /i "%ACESTEP_GEN%"=="Ninja" (
  cmake --build . -j %NUMBER_OF_PROCESSORS%
) else (
  cmake --build . --config Release -j %NUMBER_OF_PROCESSORS%
)
set "BUILD_RC=%ERRORLEVEL%"
popd
exit /b %BUILD_RC%
