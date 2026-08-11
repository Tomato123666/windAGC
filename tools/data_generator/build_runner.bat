@echo off
REM ============================================================
REM Build Wind AGC Runner
REM Prerequisite: setup.ps1 must be run once (creates rt_db_ref junction)
REM ============================================================

if not exist "..\rt_db_ref\include\rt_db_api.h" (
    echo [ERROR] rt_db_ref junction not found.
    echo   Run this first (in project root):
    echo     powershell -ExecutionPolicy Bypass -File setup.ps1
    exit /b 1
)

echo === Build Wind AGC Runner ===
cl /utf-8 /std:c++17 /EHsc /O2 ^
   /I "..\rt_db_ref\include" ^
   /I "..\rt_db_ref\src\rt_db" ^
   runner.cpp ^
   "..\rt_db_ref\src\rt_db\rt_db_api.c" ^
   /Fe:runner.exe /link kernel32.lib
if %ERRORLEVEL% EQU 0 (echo [OK] runner.exe built) else (echo [FAIL] build error)