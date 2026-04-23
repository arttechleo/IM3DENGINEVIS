@echo off
echo.
echo ============================================================
echo   MLSLabsRenderer Recovery Helper
echo ============================================================
echo.

REM Step 1: Wipe DDC
echo [1/3] Wiping DerivedDataCache...
if exist "S:\Projects\2026\UnrealEngine\AIProject\Saved\DerivedDataCache" (
    rmdir /s /q "S:\Projects\2026\UnrealEngine\AIProject\Saved\DerivedDataCache"
    echo       DDC wiped.
) else (
    echo       DDC not present — skipping.
)

REM Step 2: Check .uplugin LoadingPhase
echo.
echo [2/3] Checking MLSLabsRenderer.uplugin LoadingPhase...
"S:\EpicGamesLauncher\UE_5.7\Engine\Binaries\ThirdParty\Python3\Win64\python.exe" -c ^
  "import json,sys; d=json.load(open(r'S:\Projects\2026\UnrealEngine\AIProject\Plugins\MLSLabsRenderer\MLSLabsRenderer.uplugin')); bad=[m for m in d.get('Modules',[]) if m.get('LoadingPhase')!='PostEngineInit']; sys.exit(0 if not bad else 1)" 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo       WARN: LoadingPhase != PostEngineInit — check the .uplugin manually.
) else (
    echo       LoadingPhase OK (PostEngineInit).
)

REM Step 3: Restore torch_cuda.dll if missing
echo.
echo [3/3] Checking torch_cuda.dll...
set TORCH_DEST=S:\Projects\2026\UnrealEngine\AIProject\Plugins\MLSLabsRenderer\Source\ThirdParty\libTorch\lib\torch_cuda.dll
set TORCH_SRC=S:\Projects\2026\UnrealEngine\AIProject\libtorch-win-shared-with-deps-2.7.0+cu128\libtorch\lib\torch_cuda.dll

if exist "%TORCH_DEST%" (
    echo       torch_cuda.dll present — OK.
) else (
    echo       torch_cuda.dll MISSING.
    if exist "%TORCH_SRC%" (
        echo       Copying from libtorch source...
        copy /Y "%TORCH_SRC%" "%TORCH_DEST%" >nul
        if %ERRORLEVEL% EQU 0 (
            echo       Restored torch_cuda.dll.
        ) else (
            echo       ERROR: copy failed. Check paths manually.
        )
    ) else (
        echo       Source DLLs not found at:
        echo         %TORCH_SRC%
        echo       You need to re-download libtorch-win-shared-with-deps-2.7.0+cu128.
    )
)

echo.
echo ============================================================
echo   MLSLabsRenderer recovery complete.
echo   Run Tools\launch.bat to start the editor.
echo ============================================================
echo.
pause
