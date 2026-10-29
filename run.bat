@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" 1>&2
if errorlevel 1 exit /b 1
set "APP=%~dp0build\Debug\visual_graph_tool.exe"
if not exist "%APP%" set "APP=%~dp0build\visual_graph_tool.exe"
"%APP%" %*
exit /b %errorlevel%
