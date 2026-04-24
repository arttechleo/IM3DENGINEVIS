#Requires -Version 5.1
<#
  Download official libtorch (cu128, shared+deps 2.7.0) into Plugins/MLSLabsRenderer/libtorch
  and run fix_mlslabs.bat. Requires curl.exe and tar.exe (Windows 10+).
#>
$ErrorActionPreference = 'Stop'
$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$SavedDir = Join-Path $ProjectRoot 'Saved'
$ZipPath = Join-Path $SavedDir 'libtorch_cu128.zip'
$PluginDir = Join-Path $ProjectRoot 'Plugins\MLSLabsRenderer'
$StageDir = Join-Path $PluginDir '_libtorch_staging'
$DestLibtorch = Join-Path $PluginDir 'libtorch'
$Url = 'https://download.pytorch.org/libtorch/cu128/libtorch-win-shared-with-deps-2.7.0%2Bcu128.zip'

function Test-ZipMagic([string]$Path) {
    $fs = [System.IO.File]::OpenRead($Path)
    try {
        $b = New-Object byte[] 4
        [void]$fs.Read($b, 0, 4)
        return ($b[0] -eq 0x50 -and $b[1] -eq 0x4B -and $b[2] -eq 3 -and $b[3] -eq 4) -or ($b[0] -eq 0x50 -and $b[1] -eq 0x4B -and $b[2] -eq 5 -and $b[3] -eq 6)
    }
    finally { $fs.Dispose() }
}

New-Item -ItemType Directory -Force -Path $SavedDir | Out-Null

if (-not (Get-Command curl.exe -ErrorAction SilentlyContinue)) {
    throw 'curl.exe not on PATH (install Windows curl or use Git for Windows).'
}

Write-Host "[bootstrap_libtorch] Project: $ProjectRoot"
Write-Host "[bootstrap_libtorch] Downloading (resume supported) ~3 GiB from PyTorch..."
Write-Host "[bootstrap_libtorch] URL: $Url"

$curlArgs = @(
    '-L', '--retry', '5', '--connect-timeout', '60',
    '-C', '-',
    '-o', $ZipPath,
    $Url
)
& curl.exe @curlArgs
if ($LASTEXITCODE -ne 0) {
    throw "curl.exe failed with exit $LASTEXITCODE"
}

$len = (Get-Item -LiteralPath $ZipPath).Length
Write-Host "[bootstrap_libtorch] Downloaded file size: $len bytes"
if ($len -lt 2500000000) {
    throw "File too small ($len) - likely HTML error page or truncated download. Delete $ZipPath and retry on a network that reaches download.pytorch.org"
}
if (-not (Test-ZipMagic $ZipPath)) {
    throw "Not a ZIP (bad magic). Remove $ZipPath and retry."
}

Write-Host "[bootstrap_libtorch] Extracting (tar) to staging..."
if (Test-Path -LiteralPath $StageDir) {
    Remove-Item -LiteralPath $StageDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $StageDir | Out-Null
Push-Location $StageDir
try {
    & tar.exe -xf $ZipPath
    if ($LASTEXITCODE -ne 0) {
        throw "tar.exe extract failed: $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}

$extracted = Join-Path $StageDir 'libtorch'
if (-not (Test-Path -LiteralPath $extracted)) {
    throw "Expected folder not found: $extracted"
}

$dll = Join-Path $extracted 'lib\torch_cuda.dll'
if (-not (Test-Path -LiteralPath $dll)) {
    throw "Extracted tree missing torch_cuda.dll at $dll"
}

Write-Host "[bootstrap_libtorch] Replacing plugin libtorch folder..."
if (Test-Path -LiteralPath $DestLibtorch) {
    Write-Host "[bootstrap_libtorch] Removing old $DestLibtorch (large tree - please wait)..."
    Remove-Item -LiteralPath $DestLibtorch -Recurse -Force
}
Move-Item -LiteralPath $extracted -Destination $DestLibtorch

Remove-Item -LiteralPath $StageDir -Recurse -Force -ErrorAction SilentlyContinue

Write-Host "[bootstrap_libtorch] Running Tools\fix_mlslabs.bat /FORCE ..."
$fix = Join-Path $PSScriptRoot 'fix_mlslabs.bat'
& cmd.exe /c "`"$fix`" /FORCE"
if ($LASTEXITCODE -ne 0) {
    Write-Warning "fix_mlslabs.bat exited $LASTEXITCODE (check lib layout above)"
}

Write-Host "[bootstrap_libtorch] Done. torch_cuda.dll path:"
Write-Host (Join-Path $DestLibtorch 'lib\torch_cuda.dll')
