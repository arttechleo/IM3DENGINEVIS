@echo off
echo.
echo Running pre-flight checks...
"S:\EpicGamesLauncher\UE_5.7\Engine\Binaries\ThirdParty\Python3\Win64\python.exe" ^
  "%~dp0preflight.py"
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo PRE-FLIGHT FAILED — fix above errors before building.
    pause
    exit /b 1
)
echo.
echo Pre-flight passed. Building...
echo.
"S:\EpicGamesLauncher\UE_5.7\Engine\Build\BatchFiles\Build.bat" ^
  VirtualProductionSplatEditor Win64 Development ^
  "S:\Projects\2026\UnrealEngine\AIProject\VirtualProductionSplat.uproject" ^
  -waitmutex
