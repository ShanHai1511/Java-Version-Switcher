param(
    [string]$Version = "1.0.0",
    [switch]$Release
)

$OutDir = "build"
$Name = "jvs"
$Manifest = "resources\jvs.manifest"
$Syso = "rsrc_windows_amd64.syso"

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# 生成资源文件（内嵌 manifest 以启用视觉样式）
if (Test-Path $Syso) { Remove-Item -Force $Syso }
$rsrc = Get-Command "rsrc.exe" -ErrorAction SilentlyContinue
if (-not $rsrc) { $rsrc = Get-ChildItem "$env:USERPROFILE\go\bin\rsrc.exe" -ErrorAction SilentlyContinue }
if ($rsrc) {
    & $rsrc.Source -manifest $Manifest -o $Syso
    Write-Host "Resource embedded: $Syso" -ForegroundColor Cyan
} else {
    Write-Host "WARNING: rsrc not found, UI visual styles disabled" -ForegroundColor Yellow
}

$ldflags = "-s -w -X main.Version=$Version"

if (-not $Release) {
    Write-Host "=== Dev Build ===" -ForegroundColor Cyan
    go build -ldflags="$ldflags" -o "$OutDir\$Name.exe"
    Write-Host "Dev build complete: $OutDir\$Name.exe" -ForegroundColor Green
    return
}

Write-Host "=== Release Build ===" -ForegroundColor Cyan

go build -ldflags="$ldflags -H=windowsgui" -o "$OutDir\$Name.exe"

$upx = Get-Command upx -ErrorAction SilentlyContinue
if ($upx) {
    Write-Host "Compressing with UPX..." -ForegroundColor Yellow
    & upx --best "$OutDir\$Name.exe"
} else {
    Write-Host "UPX not found. Install it from https://upx.github.io/ for smaller binary." -ForegroundColor Yellow
}

$file = Get-Item "$OutDir\$Name.exe"
Write-Host "Build complete: $($file.FullName)" -ForegroundColor Green
Write-Host "Size: $([math]::Round($file.Length / 1KB)) KB" -ForegroundColor Green
