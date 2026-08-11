@echo off
setlocal enabledelayedexpansion
REM ============================================================
REM Wind AGC HIL Test — one-shot: generate, inject, collect, evaluate
REM Usage: run_single.bat 1    (test S1)
REM        run_single.bat all  (all 7 scenes)
REM
REM Prerequisite: rt_db_init.exe must be running!
REM ============================================================

set SCENE=%1
if "%SCENE%"=="" (echo Usage: run_single.bat ^<scene^> && exit /b 1)

set GEN=python generator.py
set RUN=runner.exe
set EVAL=python evaluate.py

echo.
echo ============================================================
echo   Wind AGC HIL Test - Scene %SCENE%
echo ============================================================
echo.

if "%SCENE%"=="all" (
    call :run_one 1 s1_baseline 0.1
    call :run_one 2 s2_wind_disturbance 0.1
    call :run_one 3 s3_freq_regulation 0.1
    call :run_one 4 s4_ramp_tracking 0.1
    call :run_one 5 s5_curtailment 0.1
    call :run_one 6 s6_safety 0.1
    call :run_one 7 s7_24h_combined 60
) else if "%SCENE%"=="1" ( call :run_one 1 s1_baseline 0.1
) else if "%SCENE%"=="2" ( call :run_one 2 s2_wind_disturbance 0.1
) else if "%SCENE%"=="3" ( call :run_one 3 s3_freq_regulation 0.1
) else if "%SCENE%"=="4" ( call :run_one 4 s4_ramp_tracking 0.1
) else if "%SCENE%"=="5" ( call :run_one 5 s5_curtailment 0.1
) else if "%SCENE%"=="6" ( call :run_one 6 s6_safety 0.1
) else if "%SCENE%"=="7" ( call :run_one 7 s7_24h_combined 60
) else (
    echo Unknown scene: %SCENE%
    exit /b 1
)

echo.
echo Done.
exit /b 0

:run_one
set SNUM=%~1
set SNAME=%~2
set STEP=%~3
echo [S%SNUM%] Generate (step=%STEP%s)...
%GEN% --scene %SNUM% --step %STEP% --no-check
if errorlevel 1 (echo [FAIL] generator error && exit /b 0)
echo [S%SNUM%] Run...
%RUN% output\%SNAME%.csv output\s%SNUM%_result.csv
if errorlevel 1 (echo [FAIL] runner error && exit /b 0)
echo [S%SNUM%] Evaluate...
%EVAL% -r output\s%SNUM%_result.csv -s %SNUM%
echo.
exit /b 0