#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# aggregate_report.py — S1-S6 HIL 汇总报告
# 遍历 test_reports/{subdir}/ 下的 s{N}_execution_result.csv,
# 调用 test_runner/evaluators/ 下的评估器 → 构建断言矩阵 → FINAL_HIL_SUMMARY.md

import sys, os, csv, importlib

GREEN="\033[92m"; RED="\033[91m"; YELLOW="\033[93m"; CYAN="\033[96m"; RESET="\033[0m"; BOLD="\033[1m"

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.dirname(SCRIPT_DIR)  # test_suite/
EVAL_DIR = os.path.join(ROOT_DIR, "test_runner", "evaluators")
sys.path.insert(0, EVAL_DIR)

SCENARIOS = {
    "S1": ("s1_evaluator", "s1_baseline"),
    "S2": ("s2_evaluator", "s2_cloud"),
    "S3": ("s3_evaluator", "s3_step"),
    "S4": ("s4_evaluator", "s4_pfr"),
    "S5": ("s5_evaluator", "s5_joint"),
    "S6": ("s6_evaluator", "s6_comms"),
}

def run_evaluator(module_name, result_csv):
    try:
        mod = importlib.import_module(module_name)
    except ImportError as e:
        print(f"{RED}[SKIP] Cannot import {module_name}: {e}{RESET}")
        return None, 0, []

    evaluator = None
    for name in dir(mod):
        obj = getattr(mod, name)
        if (isinstance(obj, type) and hasattr(obj, 'evaluate')
            and name.endswith('Evaluator') and name != 'BaseEvaluator'):
            evaluator = obj()
            break

    if evaluator is None:
        print(f"{YELLOW}[SKIP] {module_name}: no Evaluator class found{RESET}")
        return None, 0, []

    from common_evaluator import load_results
    try:
        rows = load_results(result_csv)
    except SystemExit:
        return None, 0, []

    ar = evaluator.evaluate(rows)
    return ar.summary(), ar.passed + ar.failed + ar.warnings, ar.details

def main():
    print(f"{BOLD}═══ S1-S6 HIL Aggregate Report ═══{RESET}\n")

    results = {}; all_details = {}

    for scene, (module, subdir) in SCENARIOS.items():
        n = scene[-1]
        csv_path = os.path.join(SCRIPT_DIR, subdir, f"s{n}_execution_result.csv")
        if not os.path.exists(csv_path):
            print(f"{YELLOW}[SKIP] {scene}: {csv_path} not found{RESET}")
            results[scene] = None
            continue

        passed, count, details = run_evaluator(module, csv_path)
        results[scene] = (passed, count)
        all_details[scene] = details
        status = f"{GREEN}PASS{RESET}" if passed else f"{RED}FAIL{RESET}"
        print(f"  {scene}: {status} ({count} assertions)")

    print(f"\n{BOLD}════════════════════════════════════════════════{RESET}")
    print(f"{BOLD}  S1-S6 HIL FINAL SUMMARY{RESET}")
    print(f"{BOLD}════════════════════════════════════════════════{RESET}")

    all_ids = []
    for details in all_details.values():
        for passed_flag, level, label in details:
            aid = label.split(":")[0].strip() if ":" in label else label[:8]
            if aid not in all_ids: all_ids.append(aid)

    header = f"{'Scene':6s}"
    for aid in all_ids[:8]: header += f" {aid:6s}"
    header += " RESULT"
    print(header); print("-" * len(header))

    total_pass = 0; total_count = 0
    for scene in ["S1","S2","S3","S4","S5","S6"]:
        details = all_details.get(scene, [])
        if not details:
            print(f" {scene:5s} {'--':6s}" * min(8,len(all_ids[:8])) + " --"); continue
        row = f" {scene:5s}"
        for aid in all_ids[:8]:
            found = [d for d in details if d[2].startswith(aid)]
            if found: row += f" {GREEN}PASS{RESET}" if found[0][0] else f" {RED}FAIL{RESET}"
            else: row += f" {'--':6s}"
        entry = results.get(scene)
        if entry is None:
            row += "   --"
            print(row)
            continue
        passed, count = entry
        if passed: total_pass += 1
        total_count += 1
        row += f"   {GREEN}OK{RESET}" if passed else f"   {RED}FAIL{RESET}"
        print(row)

    print(f"{'─'*len(header)}")
    print(f"  TOTAL: {total_pass}/{total_count} scenarios PASS")
    print(f"{'═'*len(header)}")

    md_path = os.path.join(SCRIPT_DIR, "FINAL_HIL_SUMMARY.md")
    with open(md_path, "w", encoding="utf-8") as f:
        f.write("# S1-S6 HIL Final Summary\n\n")
        f.write(f"**Date**: 2026-07-25 | **Arch**: v4.15 Bus-First | **S4 Eval**: v2.0 Industrial\n\n"
                f"## S4 PFR 评估器 v2.0 重构说明\n\n"
                f"- S4-A1: 死区断言改为检查 ESS 实际调度功率 (`p_ess_target_mw`) 替代 Runner 理论 PFR 值, 容忍度 0.5MW\n"
                f"- S4-A1b: 新增死区退场容忍窗口 (0.5s 过渡期), 验证 ESS 平滑退场行为\n"
                f"- S4-A2: 事件驱动场景切换断言替代绝对时间戳, 对齐 DecisionEngine 防抖迟滞 (3 周期确认)\n"
                f"- 激励源降噪: 死区频率噪声 ±0.02→±0.005Hz, 新增 15s 处明确频偏阶跃\n\n"
                f"## Result Matrix\n\n")
        f.write("| Scene | Assertions | Result |\n|:-----:|:----------:|:------:|\n")
        for scene in ["S1","S2","S3","S4","S5","S6"]:
            entry = results.get(scene)
            if entry is None:
                f.write(f"| {scene} | — | SKIP |\n")
                continue
            passed, count = entry
            if passed is None: f.write(f"| {scene} | — | SKIP |\n")
            else: f.write(f"| {scene} | {count} | {'✅ PASS' if passed else '❌ FAIL'} |\n")
        f.write(f"\n**Total**: {total_pass}/{total_count} scenarios PASS\n\n## Details\n\n")
        for scene in ["S1","S2","S3","S4","S5","S6"]:
            details = all_details.get(scene, [])
            if not details: continue
            f.write(f"### {scene}\n\n")
            for passed_flag, level, label in details:
                icon = "✅" if passed_flag else ("⚠️" if level=="WARN" else "❌")
                f.write(f"- {icon} {label}\n")
            f.write("\n")
    print(f"\n{CYAN}[SUMMARY] {md_path}{RESET}")

if __name__ == "__main__":
    main()
