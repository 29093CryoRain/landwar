[CmdletBinding()]
param(
    [string]$SourceDir = "",
    [string]$BuildDir = "",
    [string]$OutputDir = "",
    [string]$Msys2Bin = "",
    [switch]$Build,
    [switch]$Verify,
    [switch]$VerifyWindow
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($SourceDir)) { $SourceDir = $repo }
if ([string]::IsNullOrWhiteSpace($BuildDir)) { $BuildDir = Join-Path $SourceDir "build-release" }
if ([string]::IsNullOrWhiteSpace($OutputDir)) { $OutputDir = Join-Path $SourceDir "release" }

$SourceDir = (Resolve-Path $SourceDir).Path

if ($Build) {
    Push-Location $SourceDir
    try {
        & cmake --preset release
        if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
        & cmake --build --preset release
        if ($LASTEXITCODE -ne 0) { throw "CMake build failed" }
    } finally {
        Pop-Location
    }
}

$BuildDir = (Resolve-Path $BuildDir).Path
$OutputDir = [System.IO.Path]::GetFullPath($OutputDir)
$exe = Join-Path $BuildDir "landwar.exe"
$data = Join-Path $SourceDir "data"

if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
    throw "Release executable not found: $exe (build first or pass -Build)"
}

if (Test-Path -LiteralPath $OutputDir) {
    Remove-Item -LiteralPath $OutputDir -Recurse -Force
}
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null

function Copy-RequiredFile([string]$relativePath) {
    $source = Join-Path $SourceDir $relativePath
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Required release file is missing: $relativePath"
    }
    $destination = Join-Path $OutputDir $relativePath
    $parent = Split-Path -Parent $destination
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination -Force
}

Copy-Item -LiteralPath $exe -Destination (Join-Path $OutputDir "landwar.exe") -Force
Copy-RequiredFile "README.md"
Copy-RequiredFile "LICENSE"
Copy-RequiredFile "THIRD_PARTY.md"
Copy-RequiredFile "ASSET-LICENSES.md"

$configFiles = @(
    "config.jsonc", "render.jsonc", "techs.jsonc", "factions.jsonc", "units.jsonc",
    "city_shapes.jsonc", "city_icon_fits.jsonc"
)
foreach ($file in $configFiles) {
    Copy-RequiredFile (Join-Path "data" $file)
}
foreach ($file in $configFiles) {
    Copy-RequiredFile (Join-Path "data" (Join-Path "default" $file))
}
$schemaFiles = @(
    "config.schema.json", "render.schema.json", "techs.schema.json", "factions.schema.json",
    "units.schema.json", "city_shapes.schema.json", "city_icon_fits.schema.json"
)
foreach ($file in $schemaFiles) {
    Copy-RequiredFile (Join-Path "data" (Join-Path "schema" $file))
}

Get-ChildItem -LiteralPath $data -Filter "map_*.bmp" -File | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $OutputDir (Join-Path "data" $_.Name)) -Force
}
$imageFiles = @("army_base.png", "mountain.png", "ring.png", "arrow.png", "arrow2.png")
foreach ($file in $imageFiles) {
    Copy-RequiredFile (Join-Path "data" $file)
}
Get-ChildItem -LiteralPath (Join-Path $data "tower") -Filter "*.png" -File | ForEach-Object {
    $destination = Join-Path $OutputDir (Join-Path "data" (Join-Path "tower" $_.Name))
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath $_.FullName -Destination $destination -Force
}

$launcher = "@echo off`r`nchcp 65001 >nul`r`ncd /d `"%~dp0`"`r`n`"%~dp0landwar.exe`"`r`n"
Set-Content -LiteralPath (Join-Path $OutputDir "运行游戏.bat") -Value $launcher -Encoding ASCII
New-Item -ItemType Directory -Path (Join-Path $OutputDir "userdata\maps") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $OutputDir "userdata\screenshots") -Force | Out-Null

if ([string]::IsNullOrWhiteSpace($Msys2Bin)) {
    throw "Pass -Msys2Bin pointing to the MSYS2 UCRT64 bin directory to copy DLL dependencies"
}
$Msys2Bin = (Resolve-Path $Msys2Bin).Path
$objdump = Join-Path $Msys2Bin "objdump.exe"
if (-not (Test-Path -LiteralPath $objdump -PathType Leaf)) {
    throw "objdump.exe not found under $Msys2Bin"
}

$systemDll = '(?i)^(api-ms-win|KERNEL32|USER32|SHELL32|ADVAPI32|GDI32|IMM32|OLE32|OLEAUT32|SETUPAPI|VERSION|WINMM|NTDLL|SHLWAPI|MSIMG32|COMDLG32|bcrypt|secur32|ucrtbase|VCRUNTIME)[^\\/]*\.dll$'
$seen = @{}
$queue = [System.Collections.Generic.Queue[string]]::new()
$queue.Enqueue($exe)
while ($queue.Count -gt 0) {
    $binary = $queue.Dequeue()
    $lines = @(& $objdump -p $binary | Where-Object { $_ -match "DLL Name:" })
    if ($LASTEXITCODE -ne 0) { throw "objdump failed for $binary" }
    foreach ($line in $lines) {
        $dll = (($line -replace ".*DLL Name:\s*", "").Trim())
        if ([string]::IsNullOrWhiteSpace($dll) -or $dll -match $systemDll) { continue }
        $key = $dll.ToLowerInvariant()
        if ($seen.ContainsKey($key)) { continue }
        $seen[$key] = $true
        $sourceDll = Join-Path $Msys2Bin $dll
        if (-not (Test-Path -LiteralPath $sourceDll -PathType Leaf)) {
            throw "Third-party DLL dependency not found in $Msys2Bin`: $dll"
        }
        Copy-Item -LiteralPath $sourceDll -Destination (Join-Path $OutputDir $dll) -Force
        $queue.Enqueue($sourceDll)
    }
}

if ($Verify) {
    Push-Location $OutputDir
    try {
        & .\landwar.exe --validate-config
        $configExit = $LASTEXITCODE
        & .\landwar.exe --headless --seed 42 --ticks 100 --summary
        $headlessExit = $LASTEXITCODE
        $probe = Join-Path $OutputDir "userdata\write-probe.tmp"
        [System.IO.File]::WriteAllText($probe, "ok")
        Remove-Item -LiteralPath $probe -Force
        if ($configExit -ne 0 -or $headlessExit -ne 0) {
            throw "Release verification failed: config=$configExit headless=$headlessExit"
        }
    } finally {
        Pop-Location
    }
}

if ($VerifyWindow) {
    $screenshot = Join-Path $OutputDir "userdata\screenshots\release-verify.png"
    if (Test-Path -LiteralPath $screenshot) { Remove-Item -LiteralPath $screenshot -Force }
    Push-Location $OutputDir
    try {
        & .\landwar.exe --screenshot $screenshot --ticks 100 --speed 1000
        $screenshotMissing = -not (Test-Path -LiteralPath $screenshot -PathType Leaf)
        $screenshotEmpty = $screenshotMissing -or (Get-Item -LiteralPath $screenshot).Length -le 0
        if ($LASTEXITCODE -ne 0 -or $screenshotEmpty) {
            throw "Window screenshot verification failed"
        }
    } finally {
        Pop-Location
    }
}

Write-Output "Release package created: $OutputDir"
