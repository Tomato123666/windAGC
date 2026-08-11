#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ============================================================
# s5_joint_generator.py — S5 光储联合优化限电调度 测试数据生成器
# ============================================================
# 版本: v2.0  |  日期: 2026-07-25
# ============================================================
# ★ v2.0 变更: 基于 scene5_step() 控制代码完整重构
#   - 4 阶段激励剖面: 限电切入 → 稳态调度 → 盈余扰动 → 限电解除
#   - 对齐 DecisionEngine S5 触发条件: ratio<0.85 AND excess>2MW
#   - 对齐抗震荡三件套: 死区(2MW) + ESS变化率限幅(2MW/cycle) + PV锁定(3周期)
#   - 降噪: 频率噪声 ±0.005Hz, 电压噪声 ±0.002pu
# ============================================================
#
# 激励剖面设计:
#   Phase A (0~30s):   PV 80→120MW 日出爬坡 — 验证 S5 切入时序
#   Phase B (30~70s):  PV 120MW 恒定 — 验证稳态限电调度 + 抗震荡三件套
#   Phase C (70~90s):  PV 120→100→120MW 云层扰动 — 验证死区防震荡
#   Phase D (90~120s): PV 120→80MW 日落降载 — 验证限电解除 + 自愈退出
#
# 关键参数 (与 config.h + scene5_step 严格对齐):
#   S5_DEADBAND_MW         = 2.0    — 控制死区
#   S5_ESS_RATE_LIMIT_MW   = 2.0    — ESS 单周期变化率限幅
#   S5_PV_LOCK_ERROR_PCT   = 0.05   — PV 锁定触发误差
#   S5_PV_LOCK_HOLD_CYCLES = 3      — PV 锁定最小保持周期
#   DecisionEngine: ratio = plan/pv < 0.85 && excess > 2MW → S5(P=65)
#   自愈退出: ratio >= 0.85 || excess <= 2MW, 连续 3 周期 → S1
# ============================================================

import argparse
import csv
import math
import os
import random
import sys

# ============================================================
# 固定种子 — 保证测试可复现
# ============================================================
random.seed(42)

# ============================================================
# 场景参数
# ============================================================
DURATION_S          = 120.0           # 总时长 (s)
DT_S                = 0.1             # 采样步长 (s) = 100ms
NOMINAL_FREQ_HZ     = 50.00           # 额定频率
FREQ_NOISE_SIGMA    = 0.002           # 频率噪声标准差 (Hz) — 降噪
FREQ_CLIP_HZ        = 0.005           # 频率噪声限幅 (±Hz) — 降噪
NOMINAL_VOLTAGE_PU  = 1.00            # 额定电压
VOLT_NOISE_SIGMA    = 0.002           # 电压噪声标准差 (pu)
VOLT_CLIP_PU        = 0.005           # 电压噪声限幅 (±pu)
PLANT_CAPACITY_MW   = 120.0           # 电站总额定容量 (MW)
PLAN_STEADY_MW      = 80.0            # 恒定计划功率 — 触发限电的关键
IRRADIANCE_BASE     = 1000.0          # 辐照度 W/m² (充足光照)
ESS_SOC_INITIAL     = 60.0            # 初始 SOC (健康区间)
NUM_PV_INVERTERS    = 3               # 逆变器数量

# ============================================================
# PV 功率剖面定义 — 4 阶段
# ============================================================
# (start_s, end_s, pv_start_mw, pv_end_mw)
PV_SEGMENTS = [
    (0.0,   30.0,   80.0, 120.0),   # Phase A: PV 爬坡 80→120MW (日出/云消)
    (30.0,  70.0,  120.0, 120.0),   # Phase B: PV 恒定 120MW (稳态限电)
    (70.0,  75.0,  120.0, 120.0),   # Phase C 开始: 稳定
    (75.0,  80.0,  120.0, 100.0),   # Phase C: PV 骤降 120→100MW (云层)
    (80.0,  85.0,  100.0, 100.0),   # Phase C: 低谷保持
    (85.0,  90.0,  100.0, 120.0),   # Phase C: PV 回升 100→120MW
    (90.0, 120.0,  120.0,  80.0),   # Phase D: PV 降载 120→80MW (日落/限电解禁)
]

# ============================================================
# 关键阈值 (与 config.h 对齐, 供自检引用)
# ============================================================
S5_RATIO_THRESHOLD  = 0.85    # ratio = plan/pv < 0.85 → 限电
S5_EXCESS_MIN_MW    = 2.0     # excess > 2MW → 触发确认


def clamp(value: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, value))


def get_pv_power(t: float) -> float:
    """根据 PV 剖面返回当前时刻的光伏总功率"""
    for (t_start, t_end, pv_start, pv_end) in PV_SEGMENTS:
        if t_start <= t < t_end:
            if abs(pv_end - pv_start) < 1e-9:
                return pv_start
            progress = (t - t_start) / (t_end - t_start)
            return pv_start + progress * (pv_end - pv_start)

    # t == DURATION_S 边界
    last_seg = PV_SEGMENTS[-1]
    return last_seg[3]


def generate_sequence() -> list[dict]:
    """生成完整 120s 时间序列"""
    num_steps = int(DURATION_S / DT_S) + 1
    rows = []

    # 预生成噪声序列 (保持连续性)
    freq_noises = [clamp(random.gauss(0, FREQ_NOISE_SIGMA),
                         -FREQ_CLIP_HZ, FREQ_CLIP_HZ) for _ in range(num_steps)]
    volt_noises = [clamp(random.gauss(0, VOLT_NOISE_SIGMA),
                         -VOLT_CLIP_PU, VOLT_CLIP_PU) for _ in range(num_steps)]

    for i in range(num_steps):
        t = i * DT_S

        # ---- 电网频率 (死区内, 降噪) ----
        freq = NOMINAL_FREQ_HZ + freq_noises[i]

        # ---- 电网电压 ----
        voltage = NOMINAL_VOLTAGE_PU + volt_noises[i]

        # ---- 光伏功率 (4 阶段剖面) ----
        pv_total = get_pv_power(t)

        # ---- 单台逆变器功率 ----
        pv_per_inv = pv_total / NUM_PV_INVERTERS

        # ---- 计划功率 (恒定 80MW — 触发限电的关键) ----
        plan = PLAN_STEADY_MW

        # ---- 辐照度 (充足光照 — 与 PV 功率匹配) ----
        # 辐照度按比例缩放: PV=120MW @ 1000 W/m²
        irradiance = IRRADIANCE_BASE * (pv_total / 120.0)
        irradiance = clamp(irradiance, 200.0, 1000.0)

        # ---- RoCoF ----
        rocof = 0.0

        # ---- 储能 SOC (微幅漂移模拟) ----
        soc = ESS_SOC_INITIAL + 0.01 * math.sin(2 * math.pi * 0.005 * t)

        rows.append({
            "timestamp_s":          round(t, 3),
            "grid_frequency_hz":    round(freq, 4),
            "grid_voltage_pu":      round(voltage, 4),
            "rocof_hz_per_s":       round(rocof, 6),
            "freq_deviation_hz":    round(NOMINAL_FREQ_HZ - freq, 4),
            "pv_total_power_mw":    round(pv_total, 4),
            "pv_inv1_power_mw":     round(pv_per_inv, 4),
            "pv_inv2_power_mw":     round(pv_per_inv, 4),
            "pv_inv3_power_mw":     round(pv_per_inv, 4),
            "plan_power_mw":        round(plan, 4),
            "irradiance_w_per_m2":  round(irradiance, 2),
            "ess_soc_pct":          round(soc, 4),
            "ess_power_mw":         0.0,
            "comm_status":          1.0,
        })

    return rows


def write_csv(rows: list[dict], filepath: str):
    fieldnames = [
        "timestamp_s", "grid_frequency_hz", "grid_voltage_pu", "rocof_hz_per_s",
        "freq_deviation_hz", "pv_total_power_mw", "pv_inv1_power_mw",
        "pv_inv2_power_mw", "pv_inv3_power_mw", "plan_power_mw",
        "irradiance_w_per_m2", "ess_soc_pct", "ess_power_mw", "comm_status",
    ]

    os.makedirs(os.path.dirname(filepath) or ".", exist_ok=True)

    with open(filepath, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    print(f"[OK] 写入 {len(rows)} 行 → {filepath}")
    print(f"     时间跨度: {rows[0]['timestamp_s']:.1f}s ~ {rows[-1]['timestamp_s']:.1f}s")
    print(f"     采样步长: {DT_S*1000:.0f}ms")


def self_check(rows: list[dict]):
    """自检: 验证 PV 剖面关键约束与 S5 触发/退出边界"""
    errors = []
    TOL_PV = 0.5    # PV 功率容差 (MW)
    TOL_F  = 0.01   # 频率容差 (Hz)

    # 检查 1: 行数
    expected_rows = int(DURATION_S / DT_S) + 1
    if len(rows) != expected_rows:
        errors.append(f"行数异常: {len(rows)} ≠ {expected_rows}")

    # 检查 2: Phase A PV 端点
    t0 = [r for r in rows if abs(r["timestamp_s"] - 0.0) < DT_S / 2]
    if t0 and abs(t0[0]["pv_total_power_mw"] - 80.0) > TOL_PV:
        errors.append(f"t=0s PV={t0[0]['pv_total_power_mw']:.1f} ≠ 80MW")

    t30 = [r for r in rows if abs(r["timestamp_s"] - 30.0) < DT_S / 2]
    if t30 and abs(t30[0]["pv_total_power_mw"] - 120.0) > TOL_PV:
        errors.append(f"t=30s PV={t30[0]['pv_total_power_mw']:.1f} ≠ 120MW")

    # 检查 3: Phase B PV 恒定 120MW
    phase_b = [r for r in rows if 35.0 < r["timestamp_s"] < 70.0]
    for r in phase_b:
        if abs(r["pv_total_power_mw"] - 120.0) > TOL_PV:
            errors.append(f"t={r['timestamp_s']:.1f}s: Phase B PV={r['pv_total_power_mw']:.1f} ≠ 120")
            break

    # 检查 4: Phase C PV 谷底 100MW
    t80 = [r for r in rows if abs(r["timestamp_s"] - 80.0) < DT_S / 2]
    if t80 and abs(t80[0]["pv_total_power_mw"] - 100.0) > TOL_PV:
        errors.append(f"t=80s PV={t80[0]['pv_total_power_mw']:.1f} ≠ 100MW")

    # 检查 5: Phase D PV 端点 80MW
    t120 = [r for r in rows if abs(r["timestamp_s"] - 120.0) < DT_S / 2]
    if t120 and abs(t120[0]["pv_total_power_mw"] - 80.0) > TOL_PV:
        errors.append(f"t=120s PV={t120[0]['pv_total_power_mw']:.1f} ≠ 80MW")

    # 检查 6: S5 触发边界 — ratio 在 Phase A 中应跨过 0.85
    # ratio < 0.85 首次出现在 PV > 80/0.85 ≈ 94.1MW 时
    cross_found = False
    for r in rows:
        ratio = r["plan_power_mw"] / r["pv_total_power_mw"] if r["pv_total_power_mw"] > 1 else 1.0
        if ratio < S5_RATIO_THRESHOLD:
            cross_found = True
            break
    if not cross_found:
        errors.append("S5 触发边界: PV 全程未跨过 ratio<0.85 — 无法触限电")
    else:
        # 记录首次穿越
        first_cross = next((r for r in rows
                           if r["pv_total_power_mw"] > 1
                           and r["plan_power_mw"] / r["pv_total_power_mw"] < S5_RATIO_THRESHOLD), None)
        if first_cross:
            t_cross = first_cross["timestamp_s"]
            pv_cross = first_cross["pv_total_power_mw"]
            print(f"  S5 触发边界: ratio<0.85 首次 @ t={t_cross:.1f}s "
                  f"(PV={pv_cross:.1f}MW, ratio={80/pv_cross:.3f})")

    # 检查 7: S5 退出边界 — Phase D 中 ratio 应回归 ≥0.85
    phase_d_end = [r for r in rows if r["timestamp_s"] > 110.0]
    exit_ratio_found = False
    for r in phase_d_end:
        ratio = r["plan_power_mw"] / r["pv_total_power_mw"] if r["pv_total_power_mw"] > 1 else 1.0
        if ratio >= S5_RATIO_THRESHOLD:
            exit_ratio_found = True
            break
    if not exit_ratio_found:
        errors.append("S5 退出边界: Phase D 末 ratio 未回归 ≥0.85 — 无法自愈退出")

    # 检查 8: 计划功率恒定
    for r in rows:
        if abs(r["plan_power_mw"] - PLAN_STEADY_MW) > 0.001:
            errors.append(f"t={r['timestamp_s']}s: 计划功率偏离 {r['plan_power_mw']}")
            break

    # 检查 9: 频率在死区内
    for r in rows:
        if abs(r["grid_frequency_hz"] - NOMINAL_FREQ_HZ) > FREQ_CLIP_HZ + 0.001:
            errors.append(f"t={r['timestamp_s']}s: 频率越死区 {r['grid_frequency_hz']:.4f}Hz")
            break

    # 检查 10: 通讯状态
    for r in rows:
        if r["comm_status"] != 1.0:
            errors.append(f"t={r['timestamp_s']}s: 通讯异常")
            break

    if errors:
        print(f"\n[WARN] 自检发现问题 ({len(errors)} 项):")
        for e in errors[:10]:
            print(f"  ! {e}")
        if len(errors) > 10:
            print(f"  ... 还有 {len(errors) - 10} 项")
    else:
        print("[OK] 自检通过 — 所有约束满足")

    # 统计摘要
    pvs = [r["pv_total_power_mw"] for r in rows]
    ratios = [r["plan_power_mw"] / r["pv_total_power_mw"] if r["pv_total_power_mw"] > 1 else 1.0
              for r in rows]
    excesses = [r["pv_total_power_mw"] - r["plan_power_mw"] for r in rows]

    print(f"\n  PV 剖面: {min(pvs):.0f} ~ {max(pvs):.0f} MW (均值 {sum(pvs)/len(pvs):.0f} MW)")
    print(f"  计划功率: {PLAN_STEADY_MW} MW 恒定")
    print(f"  Ratio (plan/PV): [{min(ratios):.3f}, {max(ratios):.3f}]")
    print(f"  Excess (PV-plan): [{min(excesses):.0f}, {max(excesses):.0f}] MW")
    print(f"  S5 触发条件: ratio < {S5_RATIO_THRESHOLD} AND excess > {S5_EXCESS_MIN_MW}MW")
    print(f"  自愈退出条件: ratio ≥ {S5_RATIO_THRESHOLD} OR excess ≤ {S5_EXCESS_MIN_MW}MW "
          f"(连续 3 cycle)")
    print(f"  激励阶段:")
    print(f"    Phase A (0~30s):  PV {80}→{120}MW — 日出爬坡, "
          f"ratio 1.0→0.67, 验证 S5 切入")
    print(f"    Phase B (30~70s): PV {120}MW 恒定 — 稳态限电调度 + 抗震荡三件套")
    print(f"    Phase C (70~90s): PV {120}→{100}→{120}MW — 云层扰动, "
          f"验证死区防震荡")
    print(f"    Phase D (90~120s): PV {120}→{80}MW — 日落降载, "
          f"ratio 0.67→1.0, 验证限电解除")


def main():
    parser = argparse.ArgumentParser(
        description="S5 光储联合优化限电调度 测试数据生成器 v2.0",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=f"""
示例:
  python s5_joint_generator.py
  python s5_joint_generator.py --output ../test_cases/s5_custom.csv
  python s5_joint_generator.py --seed 12345
        """
    )
    parser.add_argument("--output", "-o", default=None,
                        help="输出 CSV 路径 (默认: ../../test_cases/s5_joint/s5_soc_test.csv)")
    parser.add_argument("--seed", "-s", type=int, default=None,
                        help="随机种子 (默认: 42)")
    parser.add_argument("--no-check", action="store_true",
                        help="跳过后置自检")
    args = parser.parse_args()

    if args.seed is not None:
        random.seed(args.seed)
        print(f"[INFO] 随机种子: {args.seed}")

    # 输出路径
    if args.output:
        out_path = args.output
    else:
        script_dir = os.path.dirname(os.path.abspath(__file__))
        out_path = os.path.join(script_dir, "..", "..", "test_cases", "s5_joint",
                                "s5_soc_test.csv")
    out_path = os.path.normpath(out_path)

    print(f"S5 光储联合优化限电调度 测试数据生成器 v2.0")
    print(f"  总时长:    {DURATION_S} s")
    print(f"  采样步长:  {DT_S*1000:.0f} ms")
    print(f"  计划功率:  {PLAN_STEADY_MW} MW 恒定")
    print(f"  PV 范围:   {min(s[2] for s in PV_SEGMENTS):.0f} ~ "
          f"{max(s[3] for s in PV_SEGMENTS):.0f} MW")
    print(f"  额定频率:  {NOMINAL_FREQ_HZ} Hz")
    print(f"  频率噪声:  ±{FREQ_CLIP_HZ} Hz (降噪)")
    print(f"  输出路径:  {out_path}")
    print()

    rows = generate_sequence()
    write_csv(rows, out_path)

    if not args.no_check:
        self_check(rows)

    return 0


if __name__ == "__main__":
    sys.exit(main())
