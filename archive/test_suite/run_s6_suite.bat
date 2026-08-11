@echo off
setlocal enabledelayedexpansion

set "BASE=%~dp0"
set "GEN=%BASE%data_generators\s6_comms"
set "CASE=%BASE%test_cases\s6_comms"
set "RUN=%BASE%test_runner\runners"
set "EVAL=%BASE%test_runner\evaluators"
set "RPT=%BASE%test_reports\s6_comms"

if not exist "%CASE%" mkdir "%CASE%"
if not exist "%RPT%"  mkdir "%RPT%"

echo ============================================================
echo   S6 Three-Strategy HIL Test Suite
echo ============================================================
echo.

echo [Phase 1/3] Generating test CSVs ...
pushd "%GEN%"
python s6_comms_generator.py --all
if %ERRORLEVEL% neq 0 (
    echo [FATAL] CSV generation failed
    popd
    exit /b 1
)
popd
echo [Phase 1/3] Done.
echo.

echo [Phase 2/3] Running HIL tests ...
set PASS=0
set FAIL=0

for %%S in (1 2 3) do (
    echo ------------------------------------------------------------
    if %%S==1 echo   S6-1: Hold Last Value
    if %%S==2 echo   S6-2: Safe Ramp-Down
    if %%S==3 echo   S6-3: Local Droop + MPPT
    echo ------------------------------------------------------------

    if %%S==1 set "CSV=s6_1_hlv.csv"
    if %%S==2 set "CSV=s6_2_ramp.csv"
    if %%S==3 set "CSV=s6_3_droop.csv"
    if %%S==1 set "OUT=s6_1_result.csv"
    if %%S==2 set "OUT=s6_2_result.csv"
    if %%S==3 set "OUT=s6_3_result.csv"

    copy /Y "%CASE%\!CSV!" "%CASE%\s6_comm_test.csv" >nul

    pushd "%RUN%"
    s6_runner.exe "%CASE%\s6_comm_test.csv" "%RPT%\!OUT!"
    if %ERRORLEVEL% neq 0 (
        echo   [FAIL] Runner failed
        set /a FAIL+=1
        popd
    ) else (
        popd
        pushd "%EVAL%"
        python s6_evaluator.py -r "%RPT%\!OUT!" -p "%RPT%\s6_%%S_result.png" > "%RPT%\s6_%%S_report.txt" 2>&1
        if %ERRORLEVEL% neq 0 (
            echo   [FAIL] Assertions failed
            set /a FAIL+=1
        ) else (
            echo   [PASS]
            set /a PASS+=1
        )
        popd
    )
    echo.
)

echo [Phase 2/3] Done.
echo.

echo ============================================================
echo   S6 Suite Summary
echo ============================================================
echo   PASS: %PASS% / 3
echo   FAIL: %FAIL% / 3
echo ============================================================
echo.
echo   Reports: %RPT%\s6_*_report.txt
echo   Plots:   %RPT%\s6_*_result.png
echo.

endlocal
exit /b 0
