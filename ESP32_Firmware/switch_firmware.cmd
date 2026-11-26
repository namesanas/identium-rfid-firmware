@echo off
setlocal
where py >nul 2>nul
if errorlevel 1 (
    python "%~dp0Tools\switch_firmware.py" %*
) else (
    py -3 "%~dp0Tools\switch_firmware.py" %*
)
set "switch_result=%ERRORLEVEL%"
echo.
pause
exit /b %switch_result%
