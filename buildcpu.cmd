@echo off
rem CPU-only ggml build (no CUDA/Vulkan). Use when you do not need GPU inference.
setlocal

call "%~dp0setup-vcvars.cmd"
if errorlevel 1 exit /b 1

set "ACESTEP_CMAKE_FLAGS=-DGGML_CUDA=OFF"
call "%~dp0build-common.cmd"
exit /b %ERRORLEVEL%
