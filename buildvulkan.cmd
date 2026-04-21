@echo off
rem GPU build with Vulkan backend. Requires LunarG Vulkan SDK installed.
setlocal

call "%~dp0setup-vcvars.cmd"
if errorlevel 1 exit /b 1

rem --- Sanity-check Vulkan SDK ---
if not defined VULKAN_SDK (
  echo [buildvulkan] ERROR: VULKAN_SDK is not set.
  echo   Install the LunarG Vulkan SDK from https://vulkan.lunarg.com/sdk/home
  echo   ^(the installer sets VULKAN_SDK; reopen your shell after install.^)
  exit /b 1
)
if not exist "%VULKAN_SDK%\Bin\glslc.exe" (
  echo [buildvulkan] ERROR: glslc.exe not found under %%VULKAN_SDK%%\Bin.
  echo   Your Vulkan SDK install looks incomplete. Reinstall from https://vulkan.lunarg.com/
  exit /b 1
)

set "ACESTEP_CMAKE_FLAGS=-DGGML_VULKAN=ON"
call "%~dp0build-common.cmd"
exit /b %ERRORLEVEL%
