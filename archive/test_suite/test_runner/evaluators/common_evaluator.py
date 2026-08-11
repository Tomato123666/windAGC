#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ============================================================
# common_evaluator.py — 全场景 HIL 测试评估器公共底座
# ============================================================
# 版本: v1.0  |  日期: 2026-07-24
# ============================================================
# 功能:
#   - ANSI 颜色常量
#   - 通用物理常数与安全阈值
#   - AssertionResult 断言收集器
#   - CSV 加载、统计摘要、格式化报告输出
#   - S1~S6 评估器通过 import 继承此底座, 仅需定义场景专属的
#     evaluate() 与 plot_analysis() 函数。
# ============================================================

import argparse
import csv
import math
import os
import sys

# Windows 控制台 UTF-8 兼容
if sys.platform == "win32":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

# ============================================================
# ANSI 颜色 (终端)
# ============================================================
GREEN  = "\033[92m"
RED    = "\033[91m"
YELLOW = "\033[93m"
CYAN   = "\033[96m"
RESET  = "\033[0m"
BOLD   = "\033[1m"

# ============================================================
# 通用物理常数与安全阈值
# ============================================================
NOMINAL_FREQ_HZ         = 50.00
PFR_DEADBAND_HZ         = 0.05
SOC_SAFE_MIN            = 20.0
SOC_SAFE_MAX            = 90.0
ESS_MAX_POWER_MW        = 10.0
TRACKING_ERROR_MAX_PCT  = 1.0       # 稳态段跟踪误差上限
TRANSITION_MAX_ERR_PCT  = 5.0       # 过渡段放宽限值

# 迟滞防抖窗口 (对齐 DecisionEngine 的 "连续2次确认" + 1周期初始化稳定)
# S1~S6 所有场景切换均依赖此迟滞机制, 评估器统一跳过启动/切换迟滞期
HYSTERESIS_SKIP_SECONDS = 3.0

# ★★★ v4.24: 物理合规性硬阈值 (工业级电网保护) ★★★
PLANT_CAPACITY_MW        = 120.0   # 场站额定容量 — PCC 绝对上界
PCC_MIN_MW               = 0.0     # PCC 绝对下界 — 严禁倒送电
MAX_ROCOF_HZ_PER_S       = 1.0     # 最大频率变化率 — 超过触发系统崩溃

# ============================================================
# 断言结果收集器
# ============================================================
class AssertionResult:
    """收集 PASS / FAIL / WARN 断言结果, 提供格式化报告输出"""
    def __init__(self):
        self.passed   = 0
        self.failed   = 0
        self.warnings = 0
        self.details  = []   # list of (passed: bool, level: str, label: str)

    def check(self, verdict: bool, label: str, level: str = "CRITICAL"):
        if verdict:
            self.passed += 1
            self.details.append((True, level, label))
        else:
            if level == "WARN":
                self.warnings += 1
            else:
                self.failed += 1
            self.details.append((False, level, label))

    def summary(self) -> bool:
        """True = 无 CRITICAL 失败"""
        return self.failed == 0


# ============================================================
# CSV 加载
# ============================================================
def load_results(filepath: str) -> list[dict]:
    """读取 Runner 输出的 execution_result.csv, 返回 dict 列表"""
    if not os.path.exists(filepath):
        print(f"{RED}[FATAL] 结果文件不存在: {filepath}{RESET}")
        sys.exit(1)
    rows = []
    with open(filepath, "r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for r in reader:
            rows.append(r)
    print(f"{CYAN}[LOAD] 加载 {len(rows)} 条结果记录{RESET}")
    return rows


# ============================================================
# 格式化报告输出
# ============================================================
def print_report(ar: AssertionResult, scenario_label: str = "S1"):
    """打印格式化断言报告, 包含迟滞阶段状态区块"""
    print()
    print("═" * 70)
    print(f"{BOLD}  场景 ({scenario_label}) 测试断言报告{RESET}")
    print("═" * 70)

    # ── 初始化 / 迟滞阶段状态 ──
    print(f"\n  {CYAN}┌─────────────────────────────────────────────────────┐{RESET}")
    print(f"  {CYAN}│ Initialization / Hysteresis Phase (0.0s – {HYSTERESIS_SKIP_SECONDS:.0f}.0s)     │{RESET}")
    print(f"  {CYAN}├─────────────────────────────────────────────────────┤{RESET}")
    print(f"  {CYAN}│ 状态: 合规的决策确认窗口                              │{RESET}")
    print(f"  {CYAN}│ 说明: DecisionEngine 内建 \"连续 2 次确认\" 迟滞防抖    │{RESET}")
    print(f"  {CYAN}│       前 {HYSTERESIS_SKIP_SECONDS:.0f} 个 AGC 控制周期为正常决策初始化/防抖确认期  │{RESET}")
    print(f"  {CYAN}│       评估器自动跳过此窗口, 从 t ≥ {HYSTERESIS_SKIP_SECONDS:.0f}.0s 起进行断言    │{RESET}")
    print(f"  {CYAN}│ 适用: S1~S6 所有场景切换均依赖此迟滞机制              │{RESET}")
    print(f"  {CYAN}└─────────────────────────────────────────────────────┘{RESET}")

    for passed, level, label in ar.details:
        if passed:
            mark = f"{GREEN}[PASS]{RESET}"
        else:
            mark = f"{RED}[FAIL]{RESET}" if level == "CRITICAL" else f"{YELLOW}[WARN]{RESET}"
        print(f"  {mark} {label}")

    print("─" * 70)
    total  = ar.passed + ar.failed + ar.warnings
    pct    = 100.0 * ar.passed / total if total > 0 else 0
    print(f"  {BOLD}总计: {ar.passed} PASS | {ar.failed} FAIL | {ar.warnings} WARN{RESET}")
    print(f"  {BOLD}通过率: {pct:.0f}% ({ar.passed}/{total}){RESET}")
    print("─" * 70)

    if ar.summary():
        print(f"\n  {GREEN}{BOLD}╔══════════════════════════════╗{RESET}")
        print(f"  {GREEN}{BOLD}║  [PASS] {scenario_label} Test Passed!      ║{RESET}")
        print(f"  {GREEN}{BOLD}╚══════════════════════════════╝{RESET}")
    else:
        print(f"\n  {RED}{BOLD}╔══════════════════════════════╗{RESET}")
        print(f"  {RED}{BOLD}║  [FAIL] {scenario_label} Test Failed!      ║{RESET}")
        print(f"  {RED}{BOLD}╚══════════════════════════════╝{RESET}")

    print()
    return ar.summary()


# ============================================================
# 通用统计摘要
# ============================================================
def print_statistics(rows: list[dict]):
    """输出频率、跟踪误差、场景、ESS 的通用统计"""
    def f(r, key): return float(r.get(key, 0.0))

    print(f"\n{CYAN}{BOLD}═══ 统计摘要 ═══{RESET}")

    # 频率统计
    freqs = [f(r, "grid_frequency_hz") for r in rows]
    print(f"  频率: 均值={sum(freqs)/len(freqs):.3f} Hz, "
          f"范围=[{min(freqs):.3f}, {max(freqs):.3f}] Hz, "
          f"max|Δf|={max(abs(fr - NOMINAL_FREQ_HZ) for fr in freqs):.4f} Hz")

    # 跟踪误差
    errors = []
    for r in rows:
        t = f(r, "timestamp_s")
        plan = f(r, "p_plan_mw")
        pcc  = f(r, "p_pcc_total_mw")
        if plan > 0.01:
            errors.append(abs(pcc - plan) / plan * 100)

    if errors:
        errors_sorted = sorted(errors)
        med = errors_sorted[len(errors_sorted) // 2]
        p95 = errors_sorted[int(len(errors_sorted) * 0.95)]
        print(f"  跟踪误差: 中位数={med:.3f}%, P95={p95:.3f}%, max={max(errors):.3f}%")

    # 场景统计
    scenes = [int(f(r, "active_scene")) for r in rows]
    for sid in sorted(set(scenes)):
        count = sum(1 for s in scenes if s == sid)
        print(f"  Scene={sid} 占比: {count}/{len(scenes)} ({100 * count / len(scenes):.1f}%)")

    # ESS 统计
    ess_pwrs = [f(r, "p_ess_target_mw") for r in rows]
    print(f"  ESS 功率: 范围=[{min(ess_pwrs):.2f}, {max(ess_pwrs):.2f}] MW")

    socs = [f(r, "ess_soc_pct") for r in rows]
    print(f"  ESS SOC:  范围=[{min(socs):.2f}, {max(socs):.2f}]%")
    print()


# ============================================================
# 通用 PFR 违规扫描
# ============================================================
def check_pfr_all_time(ar: AssertionResult, rows: list[dict],
                       assertion_id: str = "A1",
                       label_override: str = None):
    """扫描全过程 PFR 应为 0 的断言 (适用于 S1/S2/S3/S5/S6)"""
    def f(r, key): return float(r.get(key, 0.0))
    violations = []
    for r in rows:
        pfr = f(r, "p_pfr_mw")
        if abs(pfr) > 0.001:
            violations.append((f(r, "timestamp_s"), pfr))
    label = label_override or f"{assertion_id}: PFR ≡ 0 MW 全过程"
    ar.check(len(violations) == 0,
             f"{label}  (违规 {len(violations)} 次)", "CRITICAL")
    if violations:
        for ts, val in violations[:5]:
            ar.details.append((False, "CRITICAL", f"  ↳ t={ts:.1f}s: PFR={val:.4f} MW"))
        if len(violations) > 5:
            ar.details.append((False, "CRITICAL",
                               f"  ↳ ... 还有 {len(violations)-5} 次违规"))


# ============================================================
# 通用 Scene 编号断言 (带迟滞跳过)
# ============================================================
def check_scene_identity(ar: AssertionResult, rows: list[dict],
                         expected_scene: int, assertion_id: str = "A2"):
    """验证 t ≥ HYSTERESIS_SKIP_SECONDS 后 scene 恒为预期值"""
    def f(r, key): return float(r.get(key, 0.0))
    scene_violations = []
    hysteresis_skipped = 0
    for r in rows:
        t = f(r, "timestamp_s")
        if t < HYSTERESIS_SKIP_SECONDS:
            hysteresis_skipped += 1
            continue
        sc = f(r, "active_scene")
        if abs(sc - expected_scene) > 0.5:
            scene_violations.append((t, sc))
    ar.check(len(scene_violations) == 0,
             f"{assertion_id}: Scene ≡ {expected_scene} "
             f"(t ≥ {HYSTERESIS_SKIP_SECONDS:.0f}s, "
             f"跳过前 {hysteresis_skipped} 个迟滞确认周期)  "
             f"(场景切换 {len(scene_violations)} 次)", "CRITICAL")
    if scene_violations:
        for ts, val in scene_violations[:5]:
            ar.details.append((False, "CRITICAL",
                               f"  ↳ t={ts:.1f}s: Scene={val:.0f}"))


# ============================================================
# 通用 SOC / ESS 安全边界断言
# ============================================================
def check_soc_safety(ar: AssertionResult, rows: list[dict], assertion_id: str = "D1"):
    """验证 SOC 全程在安全区间"""
    def f(r, key): return float(r.get(key, 0.0))
    violations = []
    for r in rows:
        soc = f(r, "ess_soc_pct")
        if soc < SOC_SAFE_MIN or soc > SOC_SAFE_MAX:
            violations.append((f(r, "timestamp_s"), soc))
    ar.check(len(violations) == 0,
             f"{assertion_id}: SOC ∈ [{SOC_SAFE_MIN}%, {SOC_SAFE_MAX}%] 全过程 "
             f"(越限 {len(violations)} 次)", "CRITICAL")


def check_ess_power_limit(ar: AssertionResult, rows: list[dict], assertion_id: str = "D2"):
    """验证 ESS 功率全程在额定范围内"""
    def f(r, key): return float(r.get(key, 0.0))
    violations = []
    for r in rows:
        ess = f(r, "p_ess_target_mw")
        if abs(ess) > ESS_MAX_POWER_MW + 0.01:
            violations.append((f(r, "timestamp_s"), ess))
    ar.check(len(violations) == 0,
             f"{assertion_id}: |P_ESS| ≤ {ESS_MAX_POWER_MW} MW 全过程 "
             f"(越限 {len(violations)} 次)", "CRITICAL")


# ============================================================
# 事件驱动场景切换断言 (适用于 PFR 等频偏触发型场景)
# ============================================================
def check_scene_event_driven(ar: AssertionResult, rows: list[dict],
                              freq_field: str = "grid_frequency_hz",
                              scene_field: str = "active_scene",
                              deadband_hz: float = 0.05,
                              debounce_cycles: int = 3,
                              expected_scene: int = 4,
                              exit_scene: int = 1,
                              assertion_id: str = "A2"):
    """
    事件驱动场景切换断言 — 替代 check_scene_identity 用于 PFR 类场景。

    工业逻辑：
      场景切换由频偏事件触发，而非绝对时间戳。决策引擎在检测到 |Δf| > deadband
      后，需经过 stable_counter ≥ 2 的防抖确认才执行切换。同样，频率回归死区后
      需 heal_stable_count_ ≥ 3 的连续确认才退出。

    判定规则:
      1. 扫描 rows 找到频率首次突破死区的时间点 (t_first_cross)
      2. t_first_cross + debounce_cycles 秒后 → Scene 必须 ≡ expected_scene
      3. 频率回归死区后，允许 debounce_cycles 秒退出确认窗口
      4. 高频段（反向频偏）同样按事件触发
      5. 仅对经防抖确认后仍不匹配的周期计数为违规

    参数:
      ar              — AssertionResult 收集器
      rows            — 结果行列表
      freq_field      — 频率字段名
      scene_field     — 场景字段名
      deadband_hz     — 频率死区阈值 (Hz), 默认 0.05
      debounce_cycles — 防抖确认所需控制周期数, 默认 3
      expected_scene  — 频偏时应处于的场景号
      exit_scene      — 死区内应处于的场景号
      assertion_id    — 断言编号
    """
    def f(r, key): return float(r.get(key, 0.0))

    # ── 阶段 1: 检测频率首次突破死区 ──
    t_first_cross = None
    t_return_deadband = None
    t_second_cross = None
    t_second_return = None

    prev_in_deadband = True  # 假设从死区内开始
    for r in rows:
        t = f(r, "timestamp_s")
        freq = f(r, freq_field)
        in_deadband = abs(freq - NOMINAL_FREQ_HZ) <= deadband_hz

        # 死区 → 频偏 (下降沿)
        if prev_in_deadband and not in_deadband:
            if t_first_cross is None:
                t_first_cross = t
            elif t_second_cross is None and t_return_deadband is not None:
                t_second_cross = t

        # 频偏 → 死区 (上升沿)
        if not prev_in_deadband and in_deadband:
            if t_return_deadband is None:
                t_return_deadband = t
            elif t_second_return is None and t_second_cross is not None:
                t_second_return = t

        prev_in_deadband = in_deadband

    # ── 阶段 2: 统计违规 ──
    violations = []
    scene_stability = {"in_pfr": 0, "total": 0}  # PFR 激活期内场景稳定性

    for r in rows:
        t = f(r, "timestamp_s")
        freq = f(r, freq_field)
        sc = f(r, scene_field)
        in_deadband = abs(freq - NOMINAL_FREQ_HZ) <= deadband_hz

        # ── 低频段 (首次频偏) ──
        if t_first_cross is not None:
            t_assert_low = t_first_cross + debounce_cycles
            t_exit_low = t_return_deadband + debounce_cycles if t_return_deadband else float('inf')

            if t >= t_assert_low and t < t_return_deadband:
                # PFR 确认激活期: Scene 必须 ≡ expected_scene
                scene_stability["total"] += 1
                if abs(sc - expected_scene) <= 0.5:
                    scene_stability["in_pfr"] += 1
                else:
                    violations.append((t, sc, f"低频PFR期Scene≠{expected_scene}"))

            if t_return_deadband and t >= t_return_deadband and t < t_exit_low:
                # 退出确认窗口中: 允许 Scene 过渡
                pass

            if t >= t_exit_low and in_deadband and t_second_cross is None:
                # 退出确认后仍在死区内: Scene 应为 exit_scene
                if abs(sc - exit_scene) > 0.5 and abs(sc - expected_scene) <= 0.5:
                    violations.append((t, sc, f"死区回归后未退出PFR"))

        # ── 高频段 (反向频偏) ──
        if t_second_cross is not None:
            t_assert_hi = t_second_cross + debounce_cycles
            t_exit_hi = t_second_return + debounce_cycles if t_second_return else float('inf')

            if t >= t_assert_hi and t_second_return and t < t_second_return:
                scene_stability["total"] += 1
                if abs(sc - expected_scene) <= 0.5:
                    scene_stability["in_pfr"] += 1
                else:
                    violations.append((t, sc, f"高频PFR期Scene≠{expected_scene}"))

            if t_second_return and t >= t_second_return and t < t_exit_hi:
                pass

    # ── 阶段 3: 判定 ──
    # 3a: 场景切换违规数
    ar.check(len(violations) == 0,
             f"{assertion_id}: 事件驱动 Scene ≡ {expected_scene} "
             f"(防抖 {debounce_cycles} 周期, 死区 ±{deadband_hz}Hz)  "
             f"(违规 {len(violations)} 次)", "CRITICAL")
    if violations:
        for ts, val, desc in violations[:8]:
            ar.details.append((False, "CRITICAL",
                               f"  ↳ t={ts:.1f}s: Scene={val:.0f} ({desc})"))
        if len(violations) > 8:
            ar.details.append((False, "CRITICAL",
                               f"  ↳ ... 还有 {len(violations)-8} 次违规"))

    # 3b: PFR 激活期场景稳定性 (Scene=4 占比 ≥ 95%)
    if scene_stability["total"] > 0:
        pct = 100.0 * scene_stability["in_pfr"] / scene_stability["total"]
        ar.check(pct >= 95.0,
                 f"{assertion_id}b: PFR激活期场景稳定性 "
                 f"{scene_stability['in_pfr']}/{scene_stability['total']} "
                 f"({pct:.1f}%) ≥ 95%", "CRITICAL")

    # 3c: 必须检测到频偏事件
    ar.check(t_first_cross is not None,
             f"{assertion_id}: 检测到频率突破死区事件 "
             f"(首次 t={t_first_cross:.1f}s)" if t_first_cross
             else f"{assertion_id}: 未检测到频率突破死区事件", "CRITICAL")

    return t_first_cross, t_return_deadband, t_second_cross, t_second_return


# ============================================================
# 死区退场容忍窗口检查
# ============================================================
def check_deadband_ramp_down(ar: AssertionResult, rows: list[dict],
                              freq_field: str = "grid_frequency_hz",
                              power_field: str = "p_ess_target_mw",
                              deadband_hz: float = 0.05,
                              grace_cycles: int = 5,
                              tolerance_mw: float = 0.5,
                              assertion_id: str = "A1b"):
    """
    死区退场容忍窗口检查 — 验证 ESS 功率在频率回归死区后平滑退场。

    工业逻辑:
      频率从频偏回归死区时，主控制器通过爬坡限幅器 (3.0 MW/s) 和一阶惯性
      (τ=200ms) 平滑退场。ESS 功率不可能在 0ms 内阶跃归零，需要 0.3~0.5s
      的物理过渡期。评估器在过渡期内仅验证功率单调递减，过渡期后验证归零。

    判定规则:
      1. 检测频率回归死区的时间点
      2. 过渡期 (grace_cycles 个控制周期): ESS 功率单调递减（放电→0）或递增（充电→0）
      3. 过渡期后: |ESS 功率| < tolerance_mw
      4. 若过渡期内功率出现反向大幅波动 (>tolerance_mw 反向), 标记违规

    参数:
      ar            — AssertionResult 收集器
      rows          — 结果行列表
      freq_field    — 频率字段名
      power_field   — ESS 功率字段名
      deadband_hz   — 频率死区阈值 (Hz)
      grace_cycles  — 退场过渡容忍控制周期数
      tolerance_mw  — 过渡期后 ESS 功率容忍上限 (MW)
      assertion_id  — 断言编号
    """
    def f(r, key): return float(r.get(key, 0.0))

    # 检测死区回归事件
    return_events = []
    prev_in_deadband = True  # 初始假设在死区内
    for i, r in enumerate(rows):
        t = f(r, "timestamp_s")
        freq = f(r, freq_field)
        in_deadband = abs(freq - NOMINAL_FREQ_HZ) <= deadband_hz

        if not prev_in_deadband and in_deadband:
            return_events.append((i, t))

        prev_in_deadband = in_deadband

    if not return_events:
        ar.check(True,
                 f"{assertion_id}: 无死区回归事件 (跳过退场检查)", "WARN")
        return

    all_ok = True
    details_list = []

    for evt_idx, (start_i, t_return) in enumerate(return_events):
        # 过渡期: t_return 之后的 grace_cycles 个控制周期
        grace_end = t_return + grace_cycles
        grace_rows = [r for r in rows
                      if t_return <= f(r, "timestamp_s") <= grace_end]
        post_rows = [r for r in rows
                     if f(r, "timestamp_s") > grace_end
                     and abs(f(r, freq_field) - NOMINAL_FREQ_HZ) <= deadband_hz]

        if not grace_rows:
            continue

        # ── 过渡期内: 功率绝对值应单调递减 ──
        grace_powers = [f(r, power_field) for r in grace_rows]
        grace_abs = [abs(p) for p in grace_powers]

        # 检查单调递减 (允许 10% 噪声回弹)
        non_monotonic = 0
        for j in range(1, len(grace_abs)):
            # 仅在功率幅度超过 tolerance_mw 的 50% 时检测单调性
            # 小幅波动 (< 0.25MW @ 0.5MW 容忍度) 属于量测噪声/一阶惯性残留
            if grace_abs[j] > grace_abs[j-1] * 1.1 and grace_abs[j] > tolerance_mw * 0.5:
                non_monotonic += 1

        if non_monotonic > len(grace_abs) * 0.3 and len(grace_abs) >= 3:
            all_ok = False
            details_list.append(
                f"  ↳ 退场#{evt_idx+1} t={t_return:.1f}s: "
                f"过渡期功率非单调 ({non_monotonic}/{len(grace_abs)} 次回弹)")

        # ── 过渡期后: |ESS 功率| < tolerance_mw ──
        if post_rows:
            post_max = max(abs(f(r, power_field)) for r in post_rows)
            post_time_start = f(post_rows[0], "timestamp_s")
            post_time_end = f(post_rows[-1], "timestamp_s")
            if post_max >= tolerance_mw:
                all_ok = False
                details_list.append(
                    f"  ↳ 退场#{evt_idx+1} t∈[{post_time_start:.1f},{post_time_end:.1f}]s: "
                    f"过渡后 |ESS|max={post_max:.4f}MW ≥ {tolerance_mw}MW")
        else:
            # 无过渡后数据 (测试结束时刚好回归死区) → 仅检查过渡期末尾
            if grace_abs and grace_abs[-1] >= tolerance_mw:
                all_ok = False
                details_list.append(
                    f"  ↳ 退场#{evt_idx+1} t={t_return:.1f}s: "
                    f"过渡期末尾 |ESS|={grace_abs[-1]:.4f}MW ≥ {tolerance_mw}MW")

    event_count = len(return_events)
    if all_ok:
        ar.check(True,
                 f"{assertion_id}: 死区退场容忍 "
                 f"({event_count} 次回归, 过渡 {grace_cycles} 周期, "
                 f"容限 {tolerance_mw}MW)", "CRITICAL")
    else:
        ar.check(False,
                 f"{assertion_id}: 死区退场容忍 "
                 f"({event_count} 次回归, 过渡 {grace_cycles} 周期, "
                 f"容限 {tolerance_mw}MW)", "CRITICAL")
        for d in details_list:
            ar.details.append((False, "CRITICAL", d))


# ============================================================
# ★★★ v4.24: 物理合规性硬断言 — 电网物理规律不可违背 ★★★
# ============================================================
def check_physics_pcc_bounds(ar: AssertionResult, rows: list[dict],
                              assertion_id: str = "PHYS-1"):
    """
    PHYS-1: 场站 PCC 功率物理边界硬断言
      全程 0.0 MW ≤ P_PCC ≤ PLANT_CAPACITY_MW (120 MW)
      若超界 → 立即判定 FAIL (不可降级为 WARN)

    背景:
      日志中 P_gen=161.1MW 超出场站上限 120MW, PV+ESS 正向叠加
      击穿物理极限, 导致仿真频率冲顶 51Hz。这是底层控制逻辑的
      致命缺陷 — 物理世界不存在 161MW 的 120MW 电站。
      同样, P_gen=-32.8MW (电站变身负荷倒吸电网功率) 也是物理
      不可能状态, 源于 ESS 极性控制错误。
    """
    def f(r, key): return float(r.get(key, 0.0))
    hi_violations = []
    lo_violations = []
    for r in rows:
        t = f(r, "timestamp_s")
        pcc = f(r, "p_pcc_total_mw")
        if pcc > PLANT_CAPACITY_MW + 0.5:  # 0.5MW 浮点容差
            hi_violations.append((t, pcc))
        if pcc < PCC_MIN_MW - 0.1:  # 0.1MW 浮点容差
            lo_violations.append((t, pcc))

    total_bad = len(hi_violations) + len(lo_violations)
    if total_bad == 0:
        ar.check(True,
                 f"{assertion_id}: PCC ∈ [{PCC_MIN_MW:.0f}, {PLANT_CAPACITY_MW:.0f}] MW "
                 f"全过程 (物理合规)", "CRITICAL")
    else:
        ar.check(False,
                 f"{assertion_id}: PCC 越界 {total_bad} 次 "
                 f"(上限>{PLANT_CAPACITY_MW:.0f}MW: {len(hi_violations)}次, "
                 f"倒送<0MW: {len(lo_violations)}次) — 物理规律违背!", "CRITICAL")
        for ts, val in hi_violations[:5]:
            ar.details.append((False, "CRITICAL",
                               f"  ↳ t={ts:.1f}s: P_PCC={val:.1f}MW > {PLANT_CAPACITY_MW:.0f}MW (容量越界!)"))
        if len(hi_violations) > 5:
            ar.details.append((False, "CRITICAL",
                               f"  ↳ ... 还有 {len(hi_violations)-5} 次容量越界"))
        for ts, val in lo_violations[:5]:
            ar.details.append((False, "CRITICAL",
                               f"  ↳ t={ts:.1f}s: P_PCC={val:.1f}MW < 0MW (倒送电! 电站变身负荷)"))
        if len(lo_violations) > 5:
            ar.details.append((False, "CRITICAL",
                               f"  ↳ ... 还有 {len(lo_violations)-5} 次倒送电"))


def check_physics_rocof(ar: AssertionResult, rows: list[dict],
                         assertion_id: str = "PHYS-2"):
    """
    PHYS-2: 频率变化率硬断言
      全程 |df/dt| ≤ 1.0 Hz/s
      若超界 → 立即判定 FAIL

    背景:
      日志中 df/dt=-5.659Hz/s 已经远超真实电网保护继电器阈值
      (典型 0.5~1.0 Hz/s)。超过 1Hz/s 在真实电网中会触发大面积
      低频减载 / 系统解列。即使仿真评估器判 PASS, 数据也严重违背
      电网物理规律。
    """
    def f(r, key): return float(r.get(key, 0.0))
    violations = []
    for i in range(1, len(rows)):
        t = f(rows[i], "timestamp_s")
        t_prev = f(rows[i-1], "timestamp_s")
        dt = t - t_prev
        if dt < 0.01:
            continue  # 跳过重复时间戳
        f_curr = f(rows[i], "grid_frequency_hz")
        f_prev = f(rows[i-1], "grid_frequency_hz")
        rocof = (f_curr - f_prev) / dt
        if abs(rocof) > MAX_ROCOF_HZ_PER_S + 0.05:  # 0.05Hz/s 测量容差
            violations.append((t, rocof))

    if len(violations) == 0:
        ar.check(True,
                 f"{assertion_id}: |df/dt| ≤ {MAX_ROCOF_HZ_PER_S} Hz/s "
                 f"全过程 (RoCoF 合规)", "CRITICAL")
    else:
        ar.check(False,
                 f"{assertion_id}: |df/dt| 越界 {len(violations)} 次 "
                 f"(>{MAX_ROCOF_HZ_PER_S}Hz/s — 电网崩溃级!)", "CRITICAL")
        for ts, val in violations[:8]:
            ar.details.append((False, "CRITICAL",
                               f"  ↳ t={ts:.1f}s: df/dt={val:.3f}Hz/s > {MAX_ROCOF_HZ_PER_S}Hz/s"))
        if len(violations) > 8:
            ar.details.append((False, "CRITICAL",
                               f"  ↳ ... 还有 {len(violations)-8} 次 RoCoF 越界"))


# ============================================================
# BaseEvaluator — 全场景评估器抽象基类 (Template Method)
# ============================================================
class BaseEvaluator:
    """子类需覆盖 evaluate() 和 plot_analysis(), 调用 run() 执行全流程"""

    def __init__(self, scenario_label: str = "S?", expected_scene: int = 1):
        self.scenario_label = scenario_label
        self.expected_scene = expected_scene

    # ---- 子类必须覆盖 ----
    def evaluate(self, rows: list[dict]) -> AssertionResult:
        raise NotImplementedError("子类必须实现 evaluate()")

    def plot_analysis(self, rows: list[dict], output_png: str):
        raise NotImplementedError("子类必须实现 plot_analysis()")

    # ---- 子类可选覆盖 ----
    def default_result_csv(self) -> str:
        return f"s{self.expected_scene}_execution_result.csv"

    def default_png_path(self) -> str:
        return f"s{self.expected_scene}_analysis_plot.png"

    # ---- 模板方法 ----
    def run(self, argv: list = None) -> bool:
        import argparse as _ap
        p = _ap.ArgumentParser(description=f"{self.scenario_label} 测试评估与可视化")
        p.add_argument("--result", "-r", default=None, help="结果 CSV 路径")
        p.add_argument("--no-plot", action="store_true", help="跳过图表生成")
        p.add_argument("--plot-only", action="store_true", help="仅生成图表")
        args = p.parse_args(argv[1:] if argv else [])

        script_dir = os.path.dirname(os.path.abspath(__file__))
        result_csv = args.result or os.path.join(script_dir, self.default_result_csv())
        result_csv = os.path.normpath(result_csv)
        png_path = os.path.join(os.path.dirname(result_csv), self.default_png_path())
        png_path = os.path.normpath(png_path)

        print(f"{BOLD}{self.scenario_label} 测试评估器{RESET}")
        print(f"  结果文件: {result_csv}")

        rows = load_results(result_csv)

        if not getattr(args, 'plot_only', False):
            print_statistics(rows)
            ar = self.evaluate(rows)
            passed = print_report(ar, scenario_label=self.scenario_label)
        else:
            passed = True

        if not getattr(args, 'no_plot', False):
            self.plot_analysis(rows, png_path)

        return passed
