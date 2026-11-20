@echo off
setlocal
cd /d "%~dp0" || exit /b 1
where node.exe >nul 2>nul
if errorlevel 1 (
    echo Install Node.js 22 or later, then open this file again.
    pause
    exit /b 1
)
if exist "bridge.local.json" (
    node.exe bridge.mjs serve bridge.local.json
) else (
    node.exe bridge.mjs serve
)
if errorlevel 1 pause
