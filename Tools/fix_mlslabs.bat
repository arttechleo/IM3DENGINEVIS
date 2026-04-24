@echo off
echo.
echo ============================================================
echo   MLSLabsRenderer Recovery Helper
echo ============================================================
echo.

REM Step 1: Restore git-tracked ThirdParty DLLs
echo [1/5] Restoring git-tracked ThirdParty DLLs...
cd /d S:\Projects\2026\UnrealEngine\AIProject
git restore Plugins/MLSLabsRenderer/Source/ThirdParty/libTorch/lib/
if %ERRORLEVEL% EQU 0 (
    echo       Git-tracked DLLs restored.
) else (
    echo       WARN: git restore returned non-zero — check manually.
)

REM Step 2: Copy large torch DLLs from extracted libtorch source (if present)
echo.
echo [2/5] Copying large torch DLLs from libtorch source (if available)...
set TORCH_LIB_SRC=S:\Projects\2026\UnrealEngine\AIProject\libtorch-win-shared-with-deps-2.7.0+cu128\libtorch\lib
set TORCH_LIB_DST=S:\Projects\2026\UnrealEngine\AIProject\Plugins\MLSLabsRenderer\Source\ThirdParty\libTorch\lib

if exist "%TORCH_LIB_SRC%\torch_cuda.dll" (
    xcopy /Y /I "%TORCH_LIB_SRC%\*.dll" "%TORCH_LIB_DST%\" >nul
    xcopy /Y /I "%TORCH_LIB_SRC%\*.lib" "%TORCH_LIB_DST%\" >nul
    echo       Copied DLLs/LIBs from libtorch source.
) else (
    echo       Libtorch source not found at expected path.
    echo       Checking for libtorch_tmp.zip in Saved/...
    if exist "S:\Projects\2026\UnrealEngine\AIProject\Saved\libtorch_tmp.zip" (
        echo       Found libtorch_tmp.zip. Extracting torch_cuda.dll and deps...
        "S:\EpicGamesLauncher\UE_5.7\Engine\Binaries\ThirdParty\Python3\Win64\python.exe" -c ^
          "import zipfile,os; z=zipfile.ZipFile(r'S:\Projects\2026\UnrealEngine\AIProject\Saved\libtorch_tmp.zip'); dst=r'S:\Projects\2026\UnrealEngine\AIProject\Plugins\MLSLabsRenderer\Source\ThirdParty\libTorch\lib'; [z.extract(n,dst) if n.endswith('.dll') and 'libtorch/lib/' in n else None for n in z.namelist()]; print('Extraction complete')"
        echo       Extracted from zip.
    ) else (
        echo       WARN: torch_cuda.dll source not found.
        echo       Download libtorch-win-shared-with-deps-2.7.0+cu128 and place in project root.
    )
)

REM Step 3: Check .uplugin LoadingPhase
echo.
echo [3/5] Checking MLSLabsRenderer.uplugin LoadingPhase...
"S:\EpicGamesLauncher\UE_5.7\Engine\Binaries\ThirdParty\Python3\Win64\python.exe" -c ^
  "import json,sys; d=json.load(open(r'S:\Projects\2026\UnrealEngine\AIProject\Plugins\MLSLabsRenderer\MLSLabsRenderer.uplugin')); bad=[m for m in d.get('Modules',[]) if m.get('LoadingPhase')!='PostEngineInit']; sys.exit(0 if not bad else 1)" 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo       WARN: LoadingPhase != PostEngineInit — check the .uplugin manually.
) else (
    echo       LoadingPhase OK (PostEngineInit).
)

REM Step 4: Wipe DDC
echo.
echo [4/5] Wiping DerivedDataCache...
if exist "S:\Projects\2026\UnrealEngine\AIProject\Saved\DerivedDataCache" (
    rmdir /s /q "S:\Projects\2026\UnrealEngine\AIProject\Saved\DerivedDataCache"
    echo       DDC wiped.
) else (
    echo       DDC not present — skipping.
)

REM Step 5: Restore torch_cuda.dll if missing (final check)
echo.
echo [5/5] Final torch_cuda.dll check...
set TORCH_DEST=%TORCH_LIB_DST%\torch_cuda.dll
if exist "%TORCH_DEST%" (
    echo       torch_cuda.dll present — OK.
) else (
    echo       WARN: torch_cuda.dll still missing after all restore attempts.
    echo       GPU inference will be unavailable. Editor may still load.
)

echo.
echo ============================================================
echo   MLSLabsRenderer recovery complete.
echo   Run Tools\launch.bat to start the editor.
echo ============================================================
echo.
pause
