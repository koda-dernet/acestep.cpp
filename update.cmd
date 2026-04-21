@echo off
rem Pull latest ace / ggml, initializing the ggml submodule on first run.
setlocal
pushd "%~dp0"

where git >nul 2>&1
if errorlevel 1 (
  echo [update] ERROR: git not found. Install Git from https://git-scm.com/downloads
  popd & exit /b 1
)

rem First-time checkout: ensure the ggml submodule is populated.
if not exist ggml\CMakeLists.txt (
  echo [update] Initializing ggml submodule ...
  git submodule update --init --recursive
  if errorlevel 1 ( popd & exit /b 1 )
)

echo [update] Pulling main repo ...
git pull --rebase --autostash
if errorlevel 1 ( popd & exit /b 1 )

echo [update] Syncing ggml submodule ...
git submodule update --init --recursive
if errorlevel 1 ( popd & exit /b 1 )

echo [update] Done.
popd
exit /b 0
