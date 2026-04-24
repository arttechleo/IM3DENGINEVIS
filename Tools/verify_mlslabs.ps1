#Requires -Version 5.1
<#
.SYNOPSIS
  Read-only MLSLabsRenderer + libtorch sanity check for VirtualProductionSplat.
#>
$ErrorActionPreference = "Continue"

function Write-Status($Msg, $Color = "White") {
    Write-Host $Msg -ForegroundColor $Color
}

$Root = Split-Path -Parent $PSScriptRoot
$UProject = Join-Path $Root "VirtualProductionSplat.uproject"
$Plugin = Join-Path $Root "Plugins\MLSLabsRenderer"
$UPlugin = Join-Path $Plugin "MLSLabsRenderer.uplugin"
$BuildCs = Join-Path $Plugin "Source\MLSLabsRenderer\MLSLabsRenderer.Build.cs"
$Lt = Join-Path $Plugin "libtorch"
$LtInc = Join-Path $Lt "include"
$LtLib = Join-Path $Lt "lib"
$LtBin = Join-Path $Lt "bin"
$PlugBin = Join-Path $Plugin "Binaries\Win64"
$ProjBin = Join-Path $Root "Binaries\Win64"

$issues = 0
$warns = 0

Write-Host ""
Write-Host "=== MLSLabs verify_mlslabs.ps1 ===" -ForegroundColor Cyan
Write-Host "Project root: $Root"

# Green checks
if (Test-Path -LiteralPath $UProject) { Write-Status "[GREEN] .uproject found" "Green" } else { Write-Status "[RED] Missing .uproject" "Red"; $issues++ }
if (Test-Path -LiteralPath $Plugin) { Write-Status "[GREEN] Plugin folder exists" "Green" } else { Write-Status "[RED] Missing plugin folder" "Red"; $issues++ }
if (Test-Path -LiteralPath $UPlugin) { Write-Status "[GREEN] MLSLabsRenderer.uplugin exists" "Green" } else { Write-Status "[RED] Missing .uplugin" "Red"; $issues++ }
if (Test-Path -LiteralPath $BuildCs) { Write-Status "[GREEN] MLSLabsRenderer.Build.cs exists" "Green" } else { Write-Status "[RED] Missing Build.cs" "Red"; $issues++ }

if (Test-Path -LiteralPath $Lt) { Write-Status "[GREEN] libtorch folder exists" "Green" } else { Write-Status "[RED] libtorch folder missing" "Red"; $issues++ }
if (Test-Path -LiteralPath $LtInc) { Write-Status "[GREEN] libtorch/include exists" "Green" } else { Write-Status "[RED] libtorch/include missing" "Red"; $issues++ }

if (Test-Path -LiteralPath $LtLib) {
    $dllLib = @(Get-ChildItem -LiteralPath $LtLib -Filter "*.dll" -File -ErrorAction SilentlyContinue)
    Write-Status ("[GREEN] libtorch/lib exists - DLL count: {0}" -f $dllLib.Count) "Green"
    if ($dllLib.Count -eq 0) { Write-Status "[YELLOW] libtorch/lib has no DLLs" "Yellow"; $warns++ }
} else {
    Write-Status "[RED] libtorch/lib missing (extract shared libtorch here)" "Red"
    $issues++
}

if (Test-Path -LiteralPath $LtBin) {
    $dllBin = @(Get-ChildItem -LiteralPath $LtBin -Filter "*.dll" -File -ErrorAction SilentlyContinue)
    Write-Status ("[GREEN] libtorch/bin exists - DLL count: {0}" -f $dllBin.Count) "Green"
} else {
    Write-Status "[YELLOW] libtorch/bin not present (optional)" "Yellow"
    $warns++
}

$torchCudaInLt = (Test-Path (Join-Path $LtLib "torch_cuda.dll"))
if ($torchCudaInLt) { Write-Status "[GREEN] torch_cuda.dll under libtorch/lib" "Green" }
else { Write-Status "[YELLOW] torch_cuda.dll not in libtorch/lib" "Yellow"; $warns++ }

if (Test-Path -LiteralPath $PlugBin) {
    $pb = @(Get-ChildItem -LiteralPath $PlugBin -Filter "*.dll" -File -ErrorAction SilentlyContinue)
    Write-Status ("[GREEN] Plugin Binaries/Win64 exists - DLL count: {0}" -f $pb.Count) "Green"
} else {
    Write-Status "[YELLOW] Plugin Binaries/Win64 missing (run fix_mlslabs.bat after a build)" "Yellow"
    $warns++
}

if (Test-Path -LiteralPath $ProjBin) {
    $tb = Test-Path (Join-Path $ProjBin "torch_cuda.dll")
    if ($tb) { Write-Status "[GREEN] Project Binaries/Win64 contains torch_cuda.dll" "Green" }
    else { Write-Status "[YELLOW] Project Binaries/Win64 exists but no torch_cuda.dll copy" "Yellow"; $warns++ }
} else {
    Write-Status "[YELLOW] Project Binaries/Win64 missing (expected before first editor run)" "Yellow"
    $warns++
}

Write-Host ""
if ($issues -eq 0 -and $warns -eq 0) {
    Write-Status "SUMMARY: [GREEN] All checks passed." "Green"
    exit 0
}
if ($issues -eq 0) {
    Write-Status "SUMMARY: [YELLOW] OK with $warns warning(s)." "Yellow"
    exit 0
}
Write-Status "SUMMARY: [RED] $issues error(s), $warns warning(s)." "Red"
exit 1
