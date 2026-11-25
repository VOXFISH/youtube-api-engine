@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Build.ps1" %*
if errorlevel 1 (
    echo Build failed. See the output above.
    exit /b 1
)
echo Build complete. Run Rice.exe.
