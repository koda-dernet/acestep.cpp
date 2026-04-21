@echo off
rem -------------------------------------------------------------------------
rem setup-vcvars.cmd -- one-shot build environment for all ace* build scripts
rem
rem   * loads MSVC x64 (VS 2022/2026 Community/Pro/Enterprise/BuildTools)
rem   * ensures cmake, ninja on PATH (falls back to VS-bundled copies)
rem   * appends CUDA bin to PATH if CUDA_PATH is set
rem   * exports VSINSTALL for callers that need it (e.g. CUDA MSBuild copy)
rem
rem Usage (from another .cmd):
rem   call "%~dp0setup-vcvars.cmd"
rem   if errorlevel 1 exit /b 1
rem
rem Opt-out of the PATH reset (keep your custom PATH) by setting
rem ACESTEP_NO_PATH_RESET=1 before invoking.
rem -------------------------------------------------------------------------

rem --- 1. PATH baseline --------------------------------------------------
rem vcvars64.bat fails with "The input line is too long" if PATH is huge
rem (conda, many tools, repeated vcvars calls). Reset to a short baseline.
if not defined ACESTEP_NO_PATH_RESET (
  set "PATH=%SystemRoot%\system32;%SystemRoot%;%SystemRoot%\System32\Wbem;%SystemRoot%\System32\WindowsPowerShell\v1.0;%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
)

rem --- 2. Locate Visual Studio installation -----------------------------
set "VSINSTALL="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do set "VSINSTALL=%%i"
)

rem Fallback: well-known paths (VS 2026 / v18 first, then VS 2022). We try
rem Community / Professional / Enterprise in that order, VS 18 before 2022,
rem and the x86 BuildTools variant last.
if not defined VSINSTALL call :try_vs "%ProgramFiles%\Microsoft Visual Studio\18\Community"
if not defined VSINSTALL call :try_vs "%ProgramFiles%\Microsoft Visual Studio\18\Professional"
if not defined VSINSTALL call :try_vs "%ProgramFiles%\Microsoft Visual Studio\18\Enterprise"
if not defined VSINSTALL call :try_vs "%ProgramFiles%\Microsoft Visual Studio\2022\Community"
if not defined VSINSTALL call :try_vs "%ProgramFiles%\Microsoft Visual Studio\2022\Professional"
if not defined VSINSTALL call :try_vs "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise"
if not defined VSINSTALL call :try_vs "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools"

if not defined VSINSTALL goto :err_no_vs

rem --- 3. Call vcvars64 (x64 host) --------------------------------------
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 (
  echo [setup-vcvars] ERROR: vcvars64.bat failed. Try ACESTEP_NO_PATH_RESET=1 if your PATH is customized.
  exit /b 1
)

rem --- 4. Ensure cmake on PATH -------------------------------------------
where cmake >nul 2>&1
if errorlevel 1 if exist "%VSINSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "PATH=%VSINSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%PATH%"
where cmake >nul 2>&1
if errorlevel 1 if exist "%ProgramFiles%\CMake\bin\cmake.exe" set "PATH=%ProgramFiles%\CMake\bin;%PATH%"
where cmake >nul 2>&1
if errorlevel 1 goto :err_no_cmake

rem --- 5. Ensure ninja on PATH (for the Ninja generator) ----------------
where ninja >nul 2>&1
if errorlevel 1 if exist "%VSINSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" set "PATH=%VSINSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
where ninja >nul 2>&1
if errorlevel 1 (
  echo [setup-vcvars] WARNING: ninja not found. The Visual Studio generator will be used instead ^(slower^).
  set "ACESTEP_GEN=Visual Studio 17 2022"
) else (
  set "ACESTEP_GEN=Ninja"
)

rem --- 6. CUDA on PATH if installed -------------------------------------
if defined CUDA_PATH if exist "%CUDA_PATH%\bin\nvcc.exe" set "PATH=%CUDA_PATH%\bin;%PATH%"

exit /b 0

rem --- Helpers ---------------------------------------------------------
:try_vs
if exist "%~1\VC\Auxiliary\Build\vcvars64.bat" set "VSINSTALL=%~1"
exit /b 0

:err_no_vs
echo [setup-vcvars] ERROR: MSVC x64 tools not found.
echo   Install Visual Studio 2022 or 2026 with the "Desktop development with C++" workload.
echo   Download: https://visualstudio.microsoft.com/downloads/
exit /b 1

:err_no_cmake
echo [setup-vcvars] ERROR: cmake not found on PATH.
echo   Install "CMake tools for Windows" from the Visual Studio Installer,
echo   or download CMake from https://cmake.org/download/ and add it to PATH.
exit /b 1
