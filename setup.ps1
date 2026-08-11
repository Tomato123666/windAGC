
# ============================================================
# Wind AGC Setup Script
# Run this ONCE after cloning the repository
# Usage: powershell -ExecutionPolicy Bypass -File setup.ps1
# ============================================================

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Push-Location $root

Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  Wind Farm AGC - First Time Setup" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host ""

# ============================================================
# 1. Create rt_db_ref junction
# ============================================================
$junction = "tools\rt_db_ref"
$target = "储能协调控制器实时数据库与缓存模块详细设计\储能协调控制器实时数据库与缓存模块详细设计"

if (Test-Path $junction) {
    Write-Host "[OK] Junction already exists: $junction" -ForegroundColor Green
} else {
    Write-Host "[1/2] Creating junction: $junction -> $target" -ForegroundColor Yellow
    try {
        New-Item -ItemType Junction -Path $junction -Target $target -Force | Out-Null
        Write-Host "      Done." -ForegroundColor Green
    } catch {
        Write-Host "      FAILED: $_" -ForegroundColor Red
        Write-Host "      Please run this script as Administrator." -ForegroundColor Red
    }
}

# ============================================================
# 2. Verify dependencies
# ============================================================
Write-Host ""
Write-Host "[2/2] Checking dependencies..." -ForegroundColor Yellow

try {
    $pyVer = python --version 2>&1
    Write-Host "      Python: $pyVer" -ForegroundColor Green
} catch {
    Write-Host "      Python: NOT FOUND (required for HIL testing)" -ForegroundColor Red
}

try {
    $cmVer = cmake --version 2>&1 | Select-Object -First 1
    Write-Host "      CMake:  $cmVer" -ForegroundColor Green
} catch {
    Write-Host "      CMake:  NOT FOUND (required for building)" -ForegroundColor Red
}

Write-Host ""
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  Setup complete. Next: build_all.bat" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan

Pop-Location