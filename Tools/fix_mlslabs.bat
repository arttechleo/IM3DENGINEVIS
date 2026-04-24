@echo off
setlocal EnableExtensions EnableDelayedExpansion

REM =============================================================================
REM  MLSLabsRenderer / libtorch survival reset (VirtualProductionSplat)
REM  Run from repo root OR anywhere — paths are derived from this script.
REM  Usage:  Tools\fix_mlslabs.bat
REM          Tools\fix_mlslabs.bat /FORCE   — kill UnrealEditor.exe if running
REM =============================================================================

set "SCRIPT_DIR=%~dp0"
set "ROOT=%SCRIPT_DIR%.."
pushd "%ROOT%" >nul 2>&1
if errorlevel 1 (
  echo [FAIL] Could not cd to project root from "%SCRIPT_DIR%"
  exit /b 1
)
set "ROOT=%CD%"
echo.
echo ============================================================
echo   MLSLabsRenderer fix_mlslabs.bat
echo   Project: %ROOT%
echo ============================================================
echo.

REM --- Unreal running? ---
tasklist /FI "IMAGENAME eq UnrealEditor.exe" 2>nul | find /I "UnrealEditor.exe" >nul
if not errorlevel 1 (
  echo [WARN] Unreal Editor appears to be running.
  if /I not "%~1"=="/FORCE" (
    echo         Close the editor, then re-run this script.
    echo         Or run: Tools\fix_mlslabs.bat /FORCE
    popd
    exit /b 1
  )
  echo [INFO] /FORCE — terminating UnrealEditor.exe ...
  taskkill /F /IM UnrealEditor.exe >nul 2>&1
  timeout /t 2 /nobreak >nul
)

set "PLY=%ROOT%\VirtualProductionSplat.uproject"
if not exist "%PLY%" (
  echo [FAIL] Missing VirtualProductionSplat.uproject at "%PLY%"
  popd
  exit /b 1
)

REM --- Wipe build cruft (never Content / Config / Source) ---
echo [1/6] Deleting project Binaries, Intermediate, Saved\Logs ...
if exist "%ROOT%\Binaries" rd /s /q "%ROOT%\Binaries"
if exist "%ROOT%\Intermediate" rd /s /q "%ROOT%\Intermediate"
if exist "%ROOT%\Saved\Logs" rd /s /q "%ROOT%\Saved\Logs"
if exist "%ROOT%\Saved\libtorch_tmp.zip" (
  echo         Removing Saved\libtorch_tmp.zip (corrupt/HTML downloads break the MLSLabs setup wizard^)
  del /f /q "%ROOT%\Saved\libtorch_tmp.zip"
)

echo [2/6] Deleting plugin Binaries / Intermediate ...
if exist "%ROOT%\Plugins\MLSLabsRenderer\Binaries" rd /s /q "%ROOT%\Plugins\MLSLabsRenderer\Binaries"
if exist "%ROOT%\Plugins\MLSLabsRenderer\Intermediate" rd /s /q "%ROOT%\Plugins\MLSLabsRenderer\Intermediate"

REM --- libtorch layout ---
set "LT=%ROOT%\Plugins\MLSLabsRenderer\libtorch"
set "LT_LIB=%LT%\lib"
set "LT_BIN=%LT%\bin"
set "LT_INC=%LT%\include"

echo [3/6] Verifying libtorch folders...
set "MISS=0"
if not exist "%LT_INC%" (
  echo   [FAIL] Missing: "%LT_INC%"
  set "MISS=1"
) else (
  echo   [ OK ] include: "%LT_INC%"
)
if not exist "%LT_LIB%" (
  echo   [WARN] Missing lib folder: "%LT_LIB%" - extract libtorch shared libs here.
) else (
  echo   [ OK ] lib: "%LT_LIB%"
)
if exist "%LT_BIN%" (
  echo   [ OK ] bin: "%LT_BIN%"
) else (
  echo   [INFO] bin folder optional: "%LT_BIN%"
)

REM --- Count DLLs ---
set DLLCOUNT=0
for %%F in ("%LT_LIB%\*.dll") do set /a DLLCOUNT+=1
for %%F in ("%LT_BIN%\*.dll") do set /a DLLCOUNT+=1
echo   Torch-related DLL entries counted (lib+bin top-level^): !DLLCOUNT!
if "!DLLCOUNT!"=="0" (
  echo   [FAIL] No DLLs under libtorch. Copy from libtorch-win-shared-with-deps (e.g. 2.7+cu128^) into:
  echo          "%LT_LIB%"
  set "MISS=1"
)

REM --- Copy DLLs into Win64 folders (loader path band-aid) ---
echo [4/6] Copying torch DLLs to plugin and project Binaries\Win64 ...
set "DST_PLUGIN=%ROOT%\Plugins\MLSLabsRenderer\Binaries\Win64"
set "DST_PROJ=%ROOT%\Binaries\Win64"
mkdir "%DST_PLUGIN%" 2>nul
mkdir "%DST_PROJ%" 2>nul

if exist "%LT_LIB%" (
  xcopy /Y /I /Q "%LT_LIB%\*.dll" "%DST_PLUGIN%\" >nul 2>&1
  xcopy /Y /I /Q "%LT_LIB%\*.dll" "%DST_PROJ%\" >nul 2>&1
)
if exist "%LT_BIN%" (
  xcopy /Y /I /Q "%LT_BIN%\*.dll" "%DST_PLUGIN%\" >nul 2>&1
  xcopy /Y /I /Q "%LT_BIN%\*.dll" "%DST_PROJ%\" >nul 2>&1
)

REM --- Optional project file regen ---
echo [5/6] Regenerating Visual Studio project files (if UE_ROOT or engine on PATH) ...
set "REGEN_OK=0"
if defined UE_ROOT (
  if exist "%UE_ROOT%\Engine\Build\BatchFiles\Build.bat" (
    call "%UE_ROOT%\Engine\Build\BatchFiles\Build.bat" -projectfiles -project="%PLY%" -game -engine -progress
    if not errorlevel 1 set "REGEN_OK=1"
  )
)
if "%REGEN_OK%"=="0" (
  echo   [INFO] Skipped auto-regen. Set UE_ROOT to your Unreal Engine install root ^(folder that contains the Engine subfolder^) to enable.
  echo          Or right-click VirtualProductionSplat.uproject and choose Generate Visual Studio project files.
)

echo [6/6] Smoke check torch_cuda.dll ...
set "FOUND=0"
if exist "%DST_PLUGIN%\torch_cuda.dll" set "FOUND=1"
if exist "%DST_PROJ%\torch_cuda.dll" set "FOUND=1"
if exist "%LT_LIB%\torch_cuda.dll" set "FOUND=1"
if "%FOUND%"=="0" (
  echo   [WARN] torch_cuda.dll not found after copy - GPU torch may be missing from libtorch layout.
) else (
  echo   [ OK ] torch_cuda.dll visible in libtorch or Binaries copy targets.
)

echo.
if "!MISS!"=="1" (
  echo ============================================================
  echo   COMPLETE WITH ERRORS — fix libtorch layout, then re-run.
  echo ============================================================
  popd
  exit /b 1
)

echo ============================================================
echo   SUCCESS — cleaned intermediates and synced torch DLLs.
echo   Next: open the .uproject or run your usual build/launch script.
echo ============================================================
popd
exit /b 0
