# JVS V2 Build Script (MSVC)
param([string]$Configuration = "Debug")

$ErrorActionPreference = "Stop"

$vsDevCmd = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat"
$srcDir   = "$PSScriptRoot\src"
$outDir   = "$PSScriptRoot\build"
$target   = "$outDir\jvs_v2.exe"

if (-not (Test-Path $vsDevCmd)) { Write-Error "VsDevCmd.bat not found at $vsDevCmd" }
if (-not (Test-Path $outDir))  { New-Item -ItemType Directory -Path $outDir -Force | Out-Null }

# Compiler flags
$optFlags = if ($Configuration -eq "Debug") { "/Zi /Od /DDEBUG" } else { "/O2 /DNDEBUG" }
$cflags   = "/nologo /W4 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS $optFlags"
$libs     = "user32.lib gdi32.lib shell32.lib advapi32.lib ole32.lib comctl32.lib"

$sources = @(
    "$srcDir\main.c",
    "$srcDir\util.c",
    "$srcDir\config.c",
    "$srcDir\core.c",
    "$srcDir\gui.c"
)

# Write a temporary .cmd that sets up the VS env then compiles
$bat = @"
@echo off
call "$vsDevCmd" -arch=x64
if errorlevel 1 exit /b 1
cl $cflags /Fe:"$target" /Fo:"$outDir\\" $($sources -join ' ') /link $libs
"@

$batPath = "$outDir\_build.cmd"
Set-Content -Path $batPath -Value $bat -Encoding ASCII

Write-Host "=== Building JVS V2 ($Configuration) ==="
Write-Host "Sources: $($sources -join ', ')"
Write-Host "Output:  $target"
Write-Host ""

cmd /c "`"$batPath`"" 2>&1

if ($LASTEXITCODE -eq 0) {
    if (Test-Path $target) {
        $size = [math]::Round((Get-Item $target).Length / 1024)
        Write-Host ""
        Write-Host "Build OK: $target ($size KB)"
    } else {
        Write-Error "Build reported success but $target not found"
    }
} else {
    Write-Error "Build failed with exit code $LASTEXITCODE"
}
