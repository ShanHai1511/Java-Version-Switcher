param(
    [string]$Version = "2.0.0",
    [switch]$Release
)

$OutDir = "build"
$Name = "jvs"

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$ldflags = "-s -w -X main.Version=$Version"

if (-not $Release) {
    Write-Host "=== Dev Build ===" -ForegroundColor Cyan
    go build -ldflags="$ldflags" -o "$OutDir\$Name.exe"
    Write-Host "Dev build complete: $OutDir\$Name.exe" -ForegroundColor Green
    return
}

Write-Host "=== Release Build ===" -ForegroundColor Cyan

# 生产构建（无控制台窗口）
go build -ldflags="$ldflags -H=windowsgui" -o "$OutDir\$Name.exe"

# 检查 UPX 是否可用
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
