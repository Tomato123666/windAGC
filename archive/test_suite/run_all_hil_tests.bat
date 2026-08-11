@chcp 65001 >nul 2>&1
@echo off
setlocal enabledelayedexpansion

REM ============================================================
REM   run_all_hil_tests.bat — S1~S6 HIL Automated Test Suite v3.0
REM   2026-07-24 — 场景化子目录结构
REM
REM   前提: rt_db_init.exe + AGCSystem.exe --auto 必须先启动
REM ============================================================

set "SCRIPT_DIR=%~dp0"
set "GEN_DIR=%SCRIPT_DIR%data_generators"
set "CASE_DIR=%SCRIPT_DIR%test_cases"
set "RUN_DIR=%SCRIPT_DIR%test_runner\runners"
set "EVAL_DIR=%SCRIPT_DIR%test_runner\evaluators"
set "RPT_DIR=%SCRIPT_DIR%test_reports"

set PASS=0
set FAIL=0

echo.
echo ============================================================
echo   S1-S6 HIL Automated Test Suite v3.0
echo ============================================================
echo.

REM ############################################################
REM  Phase 1: Generate all test data
REM ############################################################
echo [1/3] Generating test data ...

REM --- S1 ---
echo   [S1] Steady-State Baseline
pushd "%GEN_DIR%\s1_baseline"
python s1_baseline_generator.py
if %ERRORLEVEL% neq 0 (echo   [FAIL] S1 & set /a FAIL+=1)
popd

REM --- S2 ---
echo   [S2] Cloud Cover
pushd "%GEN_DIR%\s2_cloud"
python s2_cloud_generator.py
if %ERRORLEVEL% neq 0 (echo   [FAIL] S2 & set /a FAIL+=1)
popd

REM --- S3 ---
echo   [S3] Setpoint Step
pushd "%GEN_DIR%\s3_step"
python s3_step_generator.py
if %ERRORLEVEL% neq 0 (echo   [FAIL] S3 & set /a FAIL+=1)
popd

REM --- S4 ---
echo   [S4] Primary Frequency Response
pushd "%GEN_DIR%\s4_pfr"
python s4_pfr_generator.py
if %ERRORLEVEL% neq 0 (echo   [FAIL] S4 & set /a FAIL+=1)
popd

REM --- S5 ---
echo   [S5] SOC Boundary Protection
pushd "%GEN_DIR%\s5_joint"
python s5_joint_generator.py
if %ERRORLEVEL% neq 0 (echo   [FAIL] S5 & set /a FAIL+=1)
popd

REM --- S6 ---
echo   [S6] Communication Loss
pushd "%GEN_DIR%\s6_comms"
python s6_comms_generator.py --all
if %ERRORLEVEL% neq 0 (echo   [FAIL] S6 & set /a FAIL+=1)
popd

echo [1/3] Data generation done.
echo.

REM ############################################################
REM  Phase 2: Run HIL tests
REM ############################################################
echo [2/3] Running HIL tests ...

REM --- S1 ---
echo ------------------------------------------------------------
echo   S1 - Steady-State Baseline (120MW, 80MW plan)
echo ------------------------------------------------------------
pushd "%RUN_DIR%"
s1_runner.exe "%CASE_DIR%\s1_baseline\s1_baseline_test.csv" "%RPT_DIR%\s1_baseline\s1_execution_result.csv"
if %ERRORLEVEL% neq 0 (echo   [FAIL] S1 runner & set /a FAIL+=1)
popd

REM --- S2 ---
echo ------------------------------------------------------------
echo   S2 - Cloud Cover / Irradiance Fluctuation
echo ------------------------------------------------------------
pushd "%RUN_DIR%"
s2_runner.exe "%CASE_DIR%\s2_cloud\s2_cloud_test.csv" "%RPT_DIR%\s2_cloud\s2_execution_result.csv"
if %ERRORLEVEL% neq 0 (echo   [FAIL] S2 runner & set /a FAIL+=1)
popd

REM --- S3 ---
echo ------------------------------------------------------------
echo   S3 - Dispatch Setpoint Step
echo ------------------------------------------------------------
pushd "%RUN_DIR%"
s3_runner.exe "%CASE_DIR%\s3_step\s3_step_test.csv" "%RPT_DIR%\s3_step\s3_execution_result.csv"
if %ERRORLEVEL% neq 0 (echo   [FAIL] S3 runner & set /a FAIL+=1)
popd

REM --- S4 ---
echo ------------------------------------------------------------
echo   S4 - Primary Frequency Response (PFR)
echo ------------------------------------------------------------
pushd "%RUN_DIR%"
s4_runner.exe "%CASE_DIR%\s4_pfr\s4_pfr_test.csv" "%RPT_DIR%\s4_pfr\s4_execution_result.csv"
if %ERRORLEVEL% neq 0 (echo   [FAIL] S4 runner & set /a FAIL+=1)
popd

REM --- S5 ---
echo ------------------------------------------------------------
echo   S5 - SOC Boundary Protection
echo ------------------------------------------------------------
pushd "%RUN_DIR%"
s5_runner.exe "%CASE_DIR%\s5_joint\s5_soc_test.csv" "%RPT_DIR%\s5_joint\s5_execution_result.csv"
if %ERRORLEVEL% neq 0 (echo   [FAIL] S5 runner & set /a FAIL+=1)
popd

REM --- S6 ---
echo ------------------------------------------------------------
echo   S6 - Communication Loss Autonomy
echo ------------------------------------------------------------
pushd "%RUN_DIR%"
s6_runner.exe "%CASE_DIR%\s6_comms\s6_comm_test.csv" "%RPT_DIR%\s6_comms\s6_execution_result.csv"
if %ERRORLEVEL% neq 0 (echo   [FAIL] S6 runner & set /a FAIL+=1)
popd

echo [2/3] HIL tests done.
echo.

REM ############################################################
REM  Phase 3: Evaluate results
REM ############################################################
echo [3/3] Running evaluators ...

REM --- S1 ---
echo ============================================================
echo   S1 Evaluation
echo ============================================================
pushd "%EVAL_DIR%"
python s1_evaluator.py
if %ERRORLEVEL% neq 0 (echo   [FAIL] S1 assertions & set /a FAIL+=1) else (set /a PASS+=1)
popd

REM --- S2 ---
echo ============================================================
echo   S2 Evaluation
echo ============================================================
pushd "%EVAL_DIR%"
python s2_evaluator.py
if %ERRORLEVEL% neq 0 (echo   [FAIL] S2 assertions & set /a FAIL+=1) else (set /a PASS+=1)
popd

REM --- S3 ---
echo ============================================================
echo   S3 Evaluation
echo ============================================================
pushd "%EVAL_DIR%"
python s3_evaluator.py
if %ERRORLEVEL% neq 0 (echo   [FAIL] S3 assertions & set /a FAIL+=1) else (set /a PASS+=1)
popd

REM --- S4 ---
echo ============================================================
echo   S4 Evaluation
echo ============================================================
pushd "%EVAL_DIR%"
python s4_evaluator.py
if %ERRORLEVEL% neq 0 (echo   [FAIL] S4 assertions & set /a FAIL+=1) else (set /a PASS+=1)
popd

REM --- S5 ---
echo ============================================================
echo   S5 Evaluation
echo ============================================================
pushd "%EVAL_DIR%"
python s5_evaluator.py
if %ERRORLEVEL% neq 0 (echo   [FAIL] S5 assertions & set /a FAIL+=1) else (set /a PASS+=1)
popd

REM --- S6 ---
echo ============================================================
echo   S6 Evaluation
echo ============================================================
pushd "%EVAL_DIR%"
python s6_evaluator.py
if %ERRORLEVEL% neq 0 (echo   [FAIL] S6 assertions & set /a FAIL+=1) else (set /a PASS+=1)
popd

REM --- Aggregate summary ---
echo ============================================================
echo   Aggregate Report
echo ============================================================
pushd "%RPT_DIR%"
python aggregate_report.py
popd

echo.
echo ============================================================
echo   All Scenarios Complete
echo   PASS: %PASS%  |  FAIL: %FAIL%
echo ============================================================

endlocal
exit /b %FAIL%
