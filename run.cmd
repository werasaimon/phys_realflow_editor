@echo off
rem Launches the editor from the build directory.
rem   run.cmd                 - the default scene
rem   run.cmd --preset 27     - the terrain scene (numbers: extern/phys_realflow/docs/README.md, "Сцены")
rem   run.cmd --software-gl   - render on the CPU (no GPU / driver trouble)
rem The build directory: RF_BUILD if set, else build\ next to this file, else %USERPROFILE%\rf-editor-build.
setlocal
set "EXE="
if defined RF_BUILD if exist "%RF_BUILD%\realflow.exe" set "EXE=%RF_BUILD%\realflow.exe"
if not defined EXE if exist "%~dp0build\realflow.exe" set "EXE=%~dp0build\realflow.exe"
if not defined EXE if exist "%USERPROFILE%\rf-editor-build\realflow.exe" set "EXE=%USERPROFILE%\rf-editor-build\realflow.exe"
if not defined EXE (
  echo realflow.exe not found. Build it first ^(README.md^) or set RF_BUILD to the build directory.
  exit /b 1
)
start "" "%EXE%" %*
