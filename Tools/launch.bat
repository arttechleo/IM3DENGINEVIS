@echo off
echo.
echo Running pre-flight checks...
"S:\EpicGamesLauncher\UE_5.7\Engine\Binaries\ThirdParty\Python3\Win64\python.exe" ^
  "%~dp0preflight.py"
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo PRE-FLIGHT FAILED — fix above errors before launching.
    pause
    exit /b 1
)
echo.
echo Pre-flight passed. Launching editor...
echo.
"S:\EpicGamesLauncher\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe" ^
  "S:\Projects\2026\UnrealEngine\AIProject\VirtualProductionSplat.uproject" ^
  -dx12
