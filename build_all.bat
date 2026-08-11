@echo off
setlocal enabledelayedexpansion

echo ============================================================
echo   Wind Farm AGC - One-Click Build Script
echo ============================================================
echo.

set BUILD_DIR=build

REM ============================================================
REM Step 0: Check environment
REM ============================================================
echo [0/6] Checking environment...

where cmake >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo   [FAIL] CMake not found!
    pause
    exit /b 1
)
echo   [OK] CMake found

cmake --help 2>nul | findstr "Visual Studio 17 2022" >nul
if %ERRORLEVEL% NEQ 0 (
    echo   [FAIL] VS2022 generator not found!
    pause
    exit /b 1
)
echo   [OK] Visual Studio 2022 generator

REM ============================================================
REM Step 1: Clean and create build directory
REM ============================================================
echo.
echo [1/6] Creating build directory...
if exist "%BUILD_DIR%" (
    echo   Cleaning old build...
    rmdir /s /q "%BUILD_DIR%"
)
mkdir "%BUILD_DIR%"
echo   [OK] build\

REM ============================================================
REM Step 2: CMake configure (run from project root, use dot)
REM ============================================================
echo.
echo [2/6] CMake configure...
cd /d "%~dp0"
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
if %ERRORLEVEL% NEQ 0 (
    echo   [FAIL] CMake configure failed!
    pause
    exit /b 1
)
echo   [OK] build\WindFarmAGC.sln

REM ============================================================
REM Step 3: Build RT_DB library
REM ============================================================
echo.
echo [3/6] Building RT_DB core library...
cmake --build build --config Release --target rt_db
if %ERRORLEVEL% NEQ 0 (
    echo   [FAIL] RT_DB library build failed!
    pause
    exit /b 1
)
echo   [OK] rt_db.lib

REM ============================================================
REM Step 4: Build RT_DB tools
REM ============================================================
echo.
echo [4/6] Building RT_DB tools...
cmake --build build --config Release --target rt_db_init
cmake --build build --config Release --target test_rt_db
cmake --build build --config Release --target capacity_test
echo   [OK] rt_db_init.exe, test_rt_db.exe, capacity_test.exe

REM ============================================================
REM Step 5: Build Unified AGC
REM ============================================================
echo.
echo [5/7] Building Unified AGC...
cmake --build build --config Release --target unified_agc
if %ERRORLEVEL% EQU 0 (echo   [OK] unified_agc) else (echo   [FAIL] unified_agc)

REM ============================================================
REM Step 6: Build all scenes
REM ============================================================
echo.
echo [6/7] Building 6 scenes...

cmake --build build --config Release --target scene1_agc_economic
if %ERRORLEVEL% EQU 0 (echo   [OK] scene1) else (echo   [FAIL] scene1)

cmake --build build --config Release --target scene2_wind_disturbance
if %ERRORLEVEL% EQU 0 (echo   [OK] scene2) else (echo   [FAIL] scene2)

cmake --build build --config Release --target scene3_freq_regulation
if %ERRORLEVEL% EQU 0 (echo   [OK] scene3) else (echo   [FAIL] scene3)

cmake --build build --config Release --target scene4_ramp_tracking
if %ERRORLEVEL% EQU 0 (echo   [OK] scene4) else (echo   [FAIL] scene4)

cmake --build build --config Release --target scene5_curtailment
if %ERRORLEVEL% EQU 0 (echo   [OK] scene5) else (echo   [FAIL] scene5)

cmake --build build --config Release --target scene6_safety_management
if %ERRORLEVEL% EQU 0 (echo   [OK] scene6) else (echo   [FAIL] scene6)

REM ============================================================
REM Step 7: Verify outputs
REM ============================================================
echo.
echo [7/7] Verifying outputs...

set BIN=build\bin\Release

if exist "%BIN%\rt_db_init.exe" (echo   [OK] rt_db_init.exe) else (echo   [MISS] rt_db_init.exe)
if exist "%BIN%\test_rt_db.exe" (echo   [OK] test_rt_db.exe) else (echo   [MISS] test_rt_db.exe)
if exist "%BIN%\capacity_test.exe" (echo   [OK] capacity_test.exe) else (echo   [MISS] capacity_test.exe)
if exist "%BIN%\unified_agc.exe" (echo   [OK] unified_agc.exe) else (echo   [MISS] unified_agc.exe)
if exist "%BIN%\scene1_agc_economic.exe" (echo   [OK] scene1_agc_economic.exe) else (echo   [MISS] scene1_agc_economic.exe)
if exist "%BIN%\scene2_wind_disturbance.exe" (echo   [OK] scene2_wind_disturbance.exe) else (echo   [MISS] scene2_wind_disturbance.exe)
if exist "%BIN%\scene3_freq_regulation.exe" (echo   [OK] scene3_freq_regulation.exe) else (echo   [MISS] scene3_freq_regulation.exe)
if exist "%BIN%\scene4_ramp_tracking.exe" (echo   [OK] scene4_ramp_tracking.exe) else (echo   [MISS] scene4_ramp_tracking.exe)
if exist "%BIN%\scene5_curtailment.exe" (echo   [OK] scene5_curtailment.exe) else (echo   [MISS] scene5_curtailment.exe)
if exist "%BIN%\scene6_safety_management.exe" (echo   [OK] scene6_safety_management.exe) else (echo   [MISS] scene6_safety_management.exe)

REM ============================================================
REM Done
REM ============================================================
echo.
echo ============================================================
echo   Build complete!
echo.
echo   Next steps:
echo   1. Start daemon:  build\bin\Release\rt_db_init.exe
echo   2. Start AGC:     build\bin\Release\unified_agc.exe
echo   3. HIL test:      cd tools\data_generator ^&^& run_single.bat 1
echo ============================================================
echo.
pause
