# ============================================================
#  Wind-AGC 风电场监控系统 — 桌面版一键启动
# ============================================================

param([Parameter(ValueFromRemainingArguments = $true)][string[]]$AppArgs)

[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8

$ScriptDir    = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot  = Split-Path -Parent (Split-Path -Parent $ScriptDir)
$BUILD_DIR    = Join-Path $ProjectRoot "build"
$BIN_DIR      = Join-Path $BUILD_DIR "bin\Release"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "    Wind-AGC 风电场监控系统" -ForegroundColor Cyan
Write-Host "    桌面版 — Qt WebEngine" -ForegroundColor DarkCyan
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
# 第 2 步：检测 / 安装 PySide6
# ===================================================================
Write-Host "  [检测] PySide6..." -ForegroundColor Yellow
& $python -c "import PySide6; from PySide6.QtWebEngineWidgets import QWebEngineView" 2>$null
if ($LASTEXITCODE -ne 0) {
    Write-Host "  [安装] 正在安装 PySide6（约 500MB，首次需要几分钟）..." -ForegroundColor Yellow
    & $python -m pip install PySide6 --quiet 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  [失败] PySide6 安装失败，请手动 pip install PySide6" -ForegroundColor Red
        pause; exit 1
    }
}
Write-Host "  [通过] PySide6 已就绪" -ForegroundColor Green

# ===================================================================
# 第 3 步：检测 / 安装 Flask 依赖
# ===================================================================
Write-Host "  [检测] Flask 依赖..." -ForegroundColor Yellow
$deps = @("flask", "flask_socketio", "flask_cors")
$missing = @()
foreach ($dep in $deps) {
    & $python -c "import $dep" 2>$null
    if ($LASTEXITCODE -ne 0) { $missing += $dep }
}
if ($missing.Count -gt 0) {
    & $python -m pip install $missing --quiet 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  [失败] 依赖安装失败: pip install $missing" -ForegroundColor Red
        pause; exit 1
    }
}
Write-Host "  [通过] Flask 依赖已就绪" -ForegroundColor Green

# ===================================================================
# 第 4 步：检测 C++ 后端（缺失时自动尝试编译）
# ===================================================================
$rtDbExe     = Join-Path $BIN_DIR "rt_db_init.exe"
$unifiedExe  = Join-Path $BIN_DIR "unified_agc.exe"
$rtDbOk      = Test-Path $rtDbExe
$uniOk       = Test-Path $unifiedExe

if ($rtDbOk -and $uniOk) {
    Write-Host "  [通过] C++ 后端已就绪" -ForegroundColor Green
} else {
    # 尝试自动编译
    $cmake = Get-Command cmake -ErrorAction SilentlyContinue
    $vsRoots = @(
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Professional",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise"
    )
    $vcvars = $null
    foreach ($vs in $vsRoots) {
        $vcv = Join-Path $vs "VC\Auxiliary\Build\vcvarsall.bat"
        if (Test-Path $vcv) { $vcvars = $vcv; break }
    }

    if ($cmake -and $vcvars) {
        Write-Host "  [编译] 自动编译 C++ 后端（静默，无弹窗）..." -ForegroundColor Yellow

        # 编译函数 — WindowStyle Hidden 确保无 cmd 弹窗
        function Invoke-SilentBuild {
            param([string]$Cmd, [string]$Label)
            Write-Host "         $Label ..." -ForegroundColor DarkGray -NoNewline
            $proc = Start-Process -FilePath "cmd.exe" -ArgumentList "/c $Cmd" -Wait -PassThru -WindowStyle Hidden
            if ($proc.ExitCode -eq 0) {
                Write-Host " 完成" -ForegroundColor Green
                return $true
            } else {
                Write-Host " 失败 (code $($proc.ExitCode))" -ForegroundColor Red
                return $false
            }
        }

        # CMake 配置
        $cfgCmd = "@echo off && call `"$vcvars`" x64 >nul 2>&1 && cmake -S `"$ProjectRoot`" -B `"$BUILD_DIR`" -G `"Visual Studio 17 2022`" -A x64 2>&1"
        $cfgOk = Invoke-SilentBuild $cfgCmd "CMake 配置"

        if ($cfgOk) {
            # 编译 rt_db_init.exe
            if (-not $rtDbOk) {
                $bld1 = "@echo off && call `"$vcvars`" x64 >nul 2>&1 && cmake --build `"$BUILD_DIR`" --config Release --target rt_db_init -- /v:minimal /nologo 2>&1"
                if (Invoke-SilentBuild $bld1 "编译 rt_db_init.exe") { $rtDbOk = $true }
            }

            # 编译 unified_agc.exe
            if (-not $uniOk) {
                $bld2 = "@echo off && call `"$vcvars`" x64 >nul 2>&1 && cmake --build `"$BUILD_DIR`" --config Release --target unified_agc -- /v:minimal /nologo 2>&1"
                if (Invoke-SilentBuild $bld2 "编译 unified_agc.exe") { $uniOk = $true }
            }
        }

        if ($rtDbOk -and $uniOk) {
            Write-Host "  [通过] C++ 后端编译完成" -ForegroundColor Green
        } else {
            Write-Host "  [提示] 自动编译未完成，将以模拟模式运行" -ForegroundColor Yellow
        }
    } else {
        if (-not $cmake) {
            Write-Host "  [提示] 未找到 CMake — 跳过 C++ 编译" -ForegroundColor Yellow
            Write-Host "         安装: winget install Kitware.CMake" -ForegroundColor DarkGray
        }
        if (-not $vcvars) {
            Write-Host "  [提示] 未找到 VS2022 — 跳过 C++ 编译" -ForegroundColor Yellow
            Write-Host "         安装 VS2022 并勾选「使用C++的桌面开发」" -ForegroundColor DarkGray
        }
        if ($rtDbOk -or $uniOk) {
            Write-Host "  [通过] C++ 后端部分就绪" -ForegroundColor Green
        } else {
            Write-Host "  [提示] 将以模拟模式运行" -ForegroundColor Yellow
        }
    }
}

# ===================================================================
# 第 5 步：启动 Qt 面板
# ===================================================================
Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
if ($rtDbOk -and $uniOk) {
    Write-Host "  全部就绪，正在启动 Qt 面板..." -ForegroundColor Green
} else {
    Write-Host "  正在启动 Qt 面板（模拟模式）..." -ForegroundColor Yellow
}
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

Push-Location $ScriptDir
try {
    $qtArgs = @("--clean") + $AppArgs
    & $python agc_qt_app.py @qtArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Host ""
        Write-Host "  [退出] Qt 面板已关闭 (代码 $LASTEXITCODE)" -ForegroundColor DarkGray
    }
} finally {
    Pop-Location
}
pause
