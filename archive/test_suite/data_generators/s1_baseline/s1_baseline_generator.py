#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ============================================================
# s1_baseline_generator.py — 场景1 (S1) 稳态基线测试数据生成器
# ============================================================
# 版本: v2.0  |  日期: 2026-07-24
# ============================================================
# 生成规格:
#   - 时长: 60 秒
#   - 采样步长: 100 ms
#   - 输出行数: 601 行 (含 t=0.0)
#   - 输出路径: ../../test_cases/s1_baseline/s1_baseline_test.csv
#
# 数据特征 (v2.0 — 120MW 真实场站数量级, 纯稳态):
#   - 电网频率:   50.00 Hz + 高斯白噪声 (±0.02 Hz 限幅, 严格 < 死区 ±0.05 Hz)
#   - 电网电压:   1.00 pu + 高斯白噪声 (±0.005 pu 限幅)
#   - 光伏功率:   80.0 MW + 0.05 Hz 正弦微幅漂移 (±2.0 MW)
#   - 计划功率:   全过程 80.0 MW 恒定基线; 30~50s 极平缓微调 +5MW (0.25 MW/s)
#   - 辐照度:     800 ± 15 W/m² (缓慢正弦, 模拟自然光照)
#   - 储能 SOC:   初始 60.0%, 微幅漂移
#   - 电站容量:   120.0 MW
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
# 场景参数 (v2.0 — 120MW 真实场站数量级)
# ============================================================
DURATION_S          = 60.0           # 总时长 (s)
DT_S                = 0.1            # 采样步长 (s) = 100ms
NOMINAL_FREQ_HZ     = 50.00          # 额定频率
FREQ_NOISE_SIGMA    = 0.008          # 频率噪声标准差 (Hz)
FREQ_CLIP_HZ        = 0.02           # 频率噪声限幅 (±Hz, < 死区 0.05)
NOMINAL_VOLTAGE_PU  = 1.00           # 额定电压
VOLT_NOISE_SIGMA    = 0.002          # 电压噪声标准差 (pu)
VOLT_CLIP_PU        = 0.005          # 电压噪声限幅 (±pu)
PLANT_CAPACITY_MW   = 120.0          # 电站总额定容量 (MW)
PV_BASE_MW          = 80.0           # 光伏额定功率 (MW) — 真实场站级: 120MW×67%≈80MW
PV_SINE_AMP_MW      = 2.0            # 光伏正弦漂移幅值 (MW) — ±2.5%
PV_SINE_FREQ_HZ     = 0.05           # 光伏正弦漂移频率 (Hz)
PLAN_STEADY_MW      = 80.0           # ★ S1 稳态: 全过程恒定计划功率 (MW)
PLAN_RAMP_START_S   = 30.0           # 微调起点 (s) — 仅极平缓过渡
PLAN_RAMP_END_S     = 50.0           # 微调终点 (s)
PLAN_MICRO_DELTA_MW = 5.0            # 微调总量 (MW) — 20s 内仅偏移 5MW
# 实际速率 = 5.0/20 = 0.25 MW/s ≪ 0.5 MW/s 上限
IRRADIANCE_BASE     = 800.0          # 基础辐照度 (W/m²)
IRRADIANCE_SINE_AMP = 15.0           # 辐照度正弦幅值 (W/m²)
IRRADIANCE_SINE_FREQ= 0.02           # 辐照度正弦频率 (Hz)
ESS_SOC_INITIAL     = 60.0           # 初始 SOC (%)
NUM_PV_INVERTERS    = 3              # S1 场景使用 3 台逆变器 (80/3≈26.67 MW/台)


def clamp(value: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, value))


def generate_plan_power(t: float) -> float:
    """S1 稳态计划功率: 全程基线 80MW; 30~50s 极平缓微调 +5MW (0.25 MW/s)"""
    if t < PLAN_RAMP_START_S:
        return PLAN_STEADY_MW
    elif t < PLAN_RAMP_END_S:
        progress = (t - PLAN_RAMP_START_S) / (PLAN_RAMP_END_S - PLAN_RAMP_START_S)
        return PLAN_STEADY_MW + progress * PLAN_MICRO_DELTA_MW
    else:
        return PLAN_STEADY_MW + PLAN_MICRO_DELTA_MW


def generate_sequence() -> list[dict]:
    """生成完整 60s 时间序列"""
    num_steps = int(DURATION_S / DT_S) + 1
    rows = []

    # 噪声种子 (预生成以保持连续性)
    freq_noises  = [clamp(random.gauss(0, FREQ_NOISE_SIGMA), -FREQ_CLIP_HZ, FREQ_CLIP_HZ) for _ in range(num_steps)]
    volt_noises  = [clamp(random.gauss(0, VOLT_NOISE_SIGMA), -VOLT_CLIP_PU, VOLT_CLIP_PU) for _ in range(num_steps)]

    for i in range(num_steps):
        t = i * DT_S

        # ---- 电网频率 ----
        freq = NOMINAL_FREQ_HZ + freq_noises[i]

        # ---- 电网电压 ----
        voltage = NOMINAL_VOLTAGE_PU + volt_noises[i]

        # ---- 光伏功率 (80MW + 0.05Hz 正弦漂移) ----
        pv_total = PV_BASE_MW + PV_SINE_AMP_MW * math.sin(2 * math.pi * PV_SINE_FREQ_HZ * t)

        # ---- 单台逆变器功率 (S1 使用 3 台, 等比例分配) ----
        pv_per_inv = pv_total / NUM_PV_INVERTERS

        # ---- 计划功率 ----
        plan = generate_plan_power(t)

        # ---- 辐照度 ----
        irradiance = IRRADIANCE_BASE + IRRADIANCE_SINE_AMP * math.sin(
            2 * math.pi * IRRADIANCE_SINE_FREQ * t)

        # ---- 储能 SOC (微幅漂移, 初始 60%) ----
        soc = ESS_SOC_INITIAL + 0.02 * math.sin(2 * math.pi * 0.01 * t)

        # ---- RoCoF (频率变化率, 近似为零 — 稳态) ----
        rocof = 0.0

        rows.append({
            "timestamp_s":          round(t, 3),
            "grid_frequency_hz":    round(freq, 4),
            "grid_voltage_pu":      round(voltage, 4),
            "rocof_hz_per_s":       round(rocof, 4),
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
    """自检: 验证关键约束"""
    errors = []

    # 检查 1: 频率严格在死区内
    for r in rows:
        dev = abs(r["grid_frequency_hz"] - NOMINAL_FREQ_HZ)
        if dev >= 0.05:
            errors.append(f"t={r['timestamp_s']}s: 频率偏差 {dev:.4f}Hz ≥ 0.05Hz (死区)")

    # 检查 2: 计划功率微调
    ramp_samples = [r for r in rows if PLAN_RAMP_START_S < r["timestamp_s"] < PLAN_RAMP_END_S]
    if ramp_samples:
        first_plan = ramp_samples[0]["plan_power_mw"]
        last_plan  = ramp_samples[-1]["plan_power_mw"]
        if abs(first_plan - PLAN_STEADY_MW) > 0.05:
            errors.append(f"微调起点计划功率: {first_plan:.2f} ≠ {PLAN_STEADY_MW}")
        if abs(last_plan - (PLAN_STEADY_MW + PLAN_MICRO_DELTA_MW)) > 0.05:
            errors.append(f"微调终点计划功率: {last_plan:.2f} ≠ {PLAN_STEADY_MW + PLAN_MICRO_DELTA_MW}")

    # 检查 3: 稳态段计划功率
    s1_steady = [r for r in rows if r["timestamp_s"] < PLAN_RAMP_START_S]
    s2_steady = [r for r in rows if r["timestamp_s"] > PLAN_RAMP_END_S]
    for r in s1_steady:
        if r["plan_power_mw"] != PLAN_STEADY_MW:
            errors.append(f"t={r['timestamp_s']}s: 首段计划功率异常 {r['plan_power_mw']}")
            break
    for r in s2_steady:
        if r["plan_power_mw"] != PLAN_STEADY_MW + PLAN_MICRO_DELTA_MW:
            errors.append(f"t={r['timestamp_s']}s: 末段计划功率异常 {r['plan_power_mw']}")
            break

    # 检查 4: 行数
    expected_rows = int(DURATION_S / DT_S) + 1
    if len(rows) != expected_rows:
        errors.append(f"行数异常: {len(rows)} ≠ {expected_rows}")

    if errors:
        print(f"\n[WARN] 自检发现问题 ({len(errors)} 项):")
        for e in errors[:10]:
            print(f"  ! {e}")
        if len(errors) > 10:
            print(f"  ... 还有 {len(errors) - 10} 项")
    else:
        print("[OK] 自检通过 — 所有约束满足")

    # 统计摘要
    freqs = [r["grid_frequency_hz"] for r in rows]
    print(f"\n  频率范围: [{min(freqs):.3f}, {max(freqs):.3f}] Hz "
          f"(max|Δf|={max(abs(f - NOMINAL_FREQ_HZ) for f in freqs):.4f} Hz)")
    print(f"  计划功率: {PLAN_STEADY_MW} MW 恒定基线 "
          f"(微调 +{PLAN_MICRO_DELTA_MW}MW @{PLAN_RAMP_START_S}s~{PLAN_RAMP_END_S}s)")
    print(f"  光伏功率: {PV_BASE_MW} ± {PV_SINE_AMP_MW} MW")


def main():
    parser = argparse.ArgumentParser(
        description="场景1 (S1) 稳态基线测试数据生成器",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=f"""
示例:
  python s1_baseline_generator.py
  python s1_baseline_generator.py --output ../test_cases/s1_custom.csv
  python s1_baseline_generator.py --seed 12345
        """
    )
    parser.add_argument("--output", "-o", default=None,
                        help="输出 CSV 路径 (默认: ../../test_cases/s1_baseline/s1_baseline_test.csv)")
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
        out_path = os.path.join(script_dir, "..", "..", "test_cases", "s1_baseline", "s1_baseline_test.csv")
    out_path = os.path.normpath(out_path)

    print(f"S1 稳态基线测试数据生成器 v2.0")
    print(f"  总时长:   {DURATION_S} s")
    print(f"  采样步长: {DT_S*1000:.0f} ms")
    print(f"  电站容量: {PLANT_CAPACITY_MW} MW")
    print(f"  额定频率: {NOMINAL_FREQ_HZ} Hz (噪声限幅 ±{FREQ_CLIP_HZ} Hz < 死区 ±0.05 Hz)")
    print(f"  额定电压: {NOMINAL_VOLTAGE_PU} pu (噪声限幅 ±{VOLT_CLIP_PU} pu)")
    print(f"  计划功率: {PLAN_STEADY_MW} MW 恒定基线 "
          f"(微调 +{PLAN_MICRO_DELTA_MW}MW @{PLAN_RAMP_START_S}s~{PLAN_RAMP_END_S}s "
          f"≈{PLAN_MICRO_DELTA_MW/(PLAN_RAMP_END_S-PLAN_RAMP_START_S):.2f} MW/s)")
    print(f"  光伏基线: {PV_BASE_MW} ± {PV_SINE_AMP_MW} MW "
          f"(每台 {PV_BASE_MW/NUM_PV_INVERTERS:.1f} MW × {NUM_PV_INVERTERS}台)")
    print(f"  输出路径: {out_path}")
    print()

    rows = generate_sequence()
    write_csv(rows, out_path)

    if not args.no_check:
        self_check(rows)

    return 0


if __name__ == "__main__":
    sys.exit(main())
