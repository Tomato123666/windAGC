# ============================================================
#  Wind-AGC 风电场监控系统 — 浏览器版一键启动
# ============================================================

param([Parameter(ValueFromRemainingArguments = $true)][string[]]$AppArgs)

[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "    Wind-AGC 风电场监控系统" -ForegroundColor Cyan
Write-Host "    浏览器模式 — Flask Web 服务器" -ForegroundColor DarkCyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# ===================================================================
# 第 1 步：检测 Python
# ===================================================================
$python = "python"
try {
    $ver = & $python --version 2>&1
    Write-Host "  [通过] Python: $ver" -ForegroundColor Green
} catch {
    Write-Host "  [失败] 未找到 Python，请安装 Python 3.8+" -ForegroundColor Red
    Write-Host "         下载: https://www.python.org/downloads/" -ForegroundColor Yellow
    pause; exit 1
}

# ===================================================================
# 第 2 步：检测 / 安装 Flask 依赖
# ===================================================================
Write-Host "  [检测] Flask 依赖..." -ForegroundColor Yellow
$deps = @("flask", "flask_socketio", "flask_cors")
$missing = @()
foreach ($dep in $deps) {
    & $python -c "import $dep" 2>$null
    if ($LASTEXITCODE -ne 0) { $missing += $dep }
}
if ($missing.Count -gt 0) {
    Write-Host "  [安装] 正在安装 $($missing -join ', ') ..." -ForegroundColor Yellow
    & $python -m pip install $missing --quiet 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  [失败] 依赖安装失败: pip install $missing" -ForegroundColor Red
        pause; exit 1
    }
}
Write-Host "  [通过] Flask 依赖已就绪" -ForegroundColor Green

# ===================================================================
# 第 3 步：启动 Flask 服务器
# ===================================================================
Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  >>  浏览器打开: http://127.0.0.1:5189  <<" -ForegroundColor Green
Write-Host "  >>  按 Ctrl+C 停止服务器               <<" -ForegroundColor Gray
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

Push-Location $ScriptDir
try {
    & $python agc_ui_server.py @AppArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Host ""
        Write-Host "  [退出] 服务器已停止 (代码 $LASTEXITCODE)" -ForegroundColor DarkGray
    }
} finally {
    Pop-Location
}
pause
