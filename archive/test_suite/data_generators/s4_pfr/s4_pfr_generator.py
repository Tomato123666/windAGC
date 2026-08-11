#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ============================================================
# s4_pfr_generator.py — 场景4 (S4) PFR 一次调频测试数据生成器
# ============================================================
# 版本: v2.0  |  日期: 2026-07-25
# ★ v2.0 变更:
#   - 降噪: 死区频率噪声 ±0.02→±0.005Hz, 避免理论 PFR 误触发
#   - 新增 15.0~15.5s 明确频偏阶跃 (50.00→49.93Hz), 确保确定性死区穿越
#   - 自检容差对齐新噪声限幅
# ============================================================
# 生成规格:
#   - 时长: 90 秒
#   - 采样步长: 100 ms
#   - 输出行数: 901 行 (含 t=0.0)
#   - 输出路径: ../../test_cases/s4_pfr/s4_pfr_test.csv
#
# 数据特征:
#   - 频率剖面 (多段 + 明确触发沿):
#       ★ v2.0 变更: 15.0~15.5s 新增 0.5s 明确频偏阶跃 (50.00→49.93Hz),
#         确保决策引擎确定性地检测到死区穿越。
#       0~15s  = 50.00 Hz (死区, ±0.005 Hz 降噪)
#       15~15.5s = 50.00→49.93 Hz 明确阶跃跨死区 (-0.14 Hz/s)
#       15.5~30s = 49.93→49.80 Hz 线性缓降 (-0.009 Hz/s)
#       30~45s = 49.80 Hz 恒定保持
#       45~55s = 49.80→50.00 Hz 线性恢复 (+0.02 Hz/s)
#       55~70s = 50.00→50.30 Hz 线性上冲 (+0.02 Hz/s)
#       70~80s = 50.30 Hz 恒定保持
#       80~90s = 50.30→50.00 Hz 线性恢复 (-0.03 Hz/s)
#   - RoCoF: 斜坡段 = Δf/Δt, 恒值段 = 0
#   - 计划功率:   全过程 80 MW 恒定
#   - 光伏功率:   80 MW (与计划匹配)
#   - 辐照度:     800 W/m² 恒定
#   - 通讯状态:   全过程 1.0
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
DURATION_S          = 90.0           # 总时长 (s)
DT_S                = 0.1            # 采样步长 (s) = 100ms
NOMINAL_FREQ_HZ     = 50.00          # 额定频率
NOMINAL_VOLTAGE_PU  = 1.00           # 额定电压
VOLT_NOISE_SIGMA    = 0.002          # 电压噪声标准差 (pu)
VOLT_CLIP_PU        = 0.005          # 电压噪声限幅 (±pu)
PLANT_CAPACITY_MW   = 120.0          # 电站总额定容量 (MW)
PV_BASE_MW          = 80.0           # 光伏额定功率 (MW) — 与计划匹配
PLAN_STEADY_MW      = 80.0           # 恒定计划功率
IRRADIANCE_BASE     = 800.0          # 辐照度 W/m²
ESS_SOC_INITIAL     = 60.0           # 初始 SOC (%)
NUM_PV_INVERTERS    = 3              # 逆变器数量

# 频率噪声 (仅用于死区段和恒值段, 不影响基准频率)
# ★ v2.0: 降噪 — 死区内理论 PFR = -60×(±0.005) = ±0.3MW, 可通过 0.5MW 容忍度
FREQ_NOISE_SIGMA    = 0.002          # 频率噪声标准差 (Hz) — 原 0.008
FREQ_NOISE_CLIP_HZ  = 0.005          # 频率噪声限幅 (±Hz) — 原 0.02

# 频率剖面定义 — 各段时间点及目标频率
# ★ v2.0: 15.0~15.5s 新增明确频偏阶跃, 确保决策引擎确定性地检测到死区穿越
# (start_s, end_s, f_start_hz, f_end_hz, has_noise)
FREQ_SEGMENTS = [
    (0.0,   15.0,  50.00, 50.00, True),    # 死区段 (降噪 ±0.005Hz)
    (15.0,  15.5,  50.00, 49.93, False),   # ★ 明确频偏阶跃 (0.5s 跨过 ±0.05Hz 死区)
    (15.5,  30.0,  49.93, 49.80, False),   # 低频缓降 (RoCoF ≈ -0.009 Hz/s)
    (30.0,  45.0,  49.80, 49.80, True),    # 低频保持 (噪声 ±0.005Hz)
    (45.0,  55.0,  49.80, 50.00, False),   # 恢复斜坡 (RoCoF = +0.02 Hz/s)
    (55.0,  70.0,  50.00, 50.30, False),   # 上冲斜坡 (RoCoF = +0.02 Hz/s)
    (70.0,  80.0,  50.30, 50.30, True),    # 高频保持 (噪声 ±0.005Hz)
    (80.0,  90.0,  50.30, 50.00, False),   # 恢复下斜坡 (RoCoF = -0.03 Hz/s)
]


def clamp(value: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, value))


def generate_frequency(t: float, noise: float) -> tuple[float, float]:
    """
    返回 (frequency_hz, rocof_hz_per_s)
    根据 t 所在段计算基准频率和 RoCoF
    """
    for (t_start, t_end, f_start, f_end, has_noise) in FREQ_SEGMENTS:
        if t_start <= t < t_end:
            if abs(f_end - f_start) < 1e-9:
                # 恒值段: RoCoF = 0
                rocof = 0.0
                if has_noise:
                    return f_start + noise, rocof
                else:
                    return f_start, rocof
            else:
                # 斜坡段: 线性插值
                progress = (t - t_start) / (t_end - t_start)
                f_base = f_start + progress * (f_end - f_start)
                rocof = (f_end - f_start) / (t_end - t_start)
                return f_base, rocof

    # t == DURATION_S 边界情况
    if t >= DURATION_S - 1e-9:
        last_seg = FREQ_SEGMENTS[-1]
        return last_seg[3], 0.0

    # fallback
    return NOMINAL_FREQ_HZ, 0.0


def generate_sequence() -> list[dict]:
    """生成完整 90s 时间序列"""
    num_steps = int(DURATION_S / DT_S) + 1
    rows = []

    # 预生成噪声序列 (保持连续性)
    freq_noises = [clamp(random.gauss(0, FREQ_NOISE_SIGMA),
                         -FREQ_NOISE_CLIP_HZ, FREQ_NOISE_CLIP_HZ) for _ in range(num_steps)]
    volt_noises = [clamp(random.gauss(0, VOLT_NOISE_SIGMA),
                         -VOLT_CLIP_PU, VOLT_CLIP_PU) for _ in range(num_steps)]

    for i in range(num_steps):
        t = i * DT_S

        # ---- 电网频率 (PFR 剖面) + RoCoF ----
        freq, rocof = generate_frequency(t, freq_noises[i])

        # ---- 电网电压 ----
        voltage = NOMINAL_VOLTAGE_PU + volt_noises[i]

        # ---- 光伏功率 (80 MW, 与计划匹配) ----
        pv_total = PV_BASE_MW

        # ---- 单台逆变器功率 ----
        pv_per_inv = pv_total / NUM_PV_INVERTERS

        # ---- 计划功率 (恒定) ----
        plan = PLAN_STEADY_MW

        # ---- 辐照度 ----
        irradiance = IRRADIANCE_BASE

        # ---- 储能 SOC (微幅漂移) ----
        soc = ESS_SOC_INITIAL + 0.02 * math.sin(2 * math.pi * 0.01 * t)

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
    """自检: 验证 PFR 频率剖面关键约束"""
    errors = []
    TOL = 0.015  # 频率容差 (噪声限幅 ±0.005 Hz, 留余量) — v2.0 降噪

    # 检查 1: 行数
    expected_rows = int(DURATION_S / DT_S) + 1
    if len(rows) != expected_rows:
        errors.append(f"行数异常: {len(rows)} ≠ {expected_rows}")

    # 检查 2: 恒值段 RoCoF = 0
    for seg in FREQ_SEGMENTS:
        t_start, t_end, f_start, f_end, has_noise = seg
        if abs(f_end - f_start) < 1e-9:
            seg_samples = [r for r in rows if t_start < r["timestamp_s"] < t_end]
            for r in seg_samples:
                if abs(r["rocof_hz_per_s"]) > 1e-6:
                    errors.append(f"t={r['timestamp_s']}s: 恒值段 RoCoF 非零 "
                                  f"({r['rocof_hz_per_s']})")
                    break

    # 检查 3: 斜坡段 RoCoF 符号与方向匹配
    for seg in FREQ_SEGMENTS:
        t_start, t_end, f_start, f_end, has_noise = seg
        if abs(f_end - f_start) > 1e-9:
            expected_sign = 1 if f_end > f_start else -1
            seg_samples = [r for r in rows if t_start < r["timestamp_s"] < t_end]
            for r in seg_samples:
                if r["rocof_hz_per_s"] * expected_sign < 0:
                    errors.append(f"t={r['timestamp_s']}s: 斜坡段 RoCoF 符号异常 "
                                  f"({r['rocof_hz_per_s']})")
                    break

    # 检查 4: 死区段频率接近 50 Hz (考虑噪声容差)
    deadband_samples = [r for r in rows if r["timestamp_s"] < 15.0]
    for r in deadband_samples:
        if abs(r["grid_frequency_hz"] - NOMINAL_FREQ_HZ) > FREQ_NOISE_CLIP_HZ + 0.001:
            errors.append(f"t={r['timestamp_s']}s: 死区频率异常 "
                          f"{r['grid_frequency_hz']:.4f} Hz")
            break

    # 检查 5: 低频保持段 (~49.80 Hz)
    low_hold = [r for r in rows if 30.0 < r["timestamp_s"] < 45.0]
    if low_hold:
        avg_f = sum(r["grid_frequency_hz"] for r in low_hold) / len(low_hold)
        if abs(avg_f - 49.80) > TOL:
            errors.append(f"低频保持段平均频率 {avg_f:.4f} Hz ≠ 49.80 Hz")

    # 检查 6: 高频保持段 (~50.30 Hz)
    high_hold = [r for r in rows if 70.0 < r["timestamp_s"] < 80.0]
    if high_hold:
        avg_f = sum(r["grid_frequency_hz"] for r in high_hold) / len(high_hold)
        if abs(avg_f - 50.30) > TOL:
            errors.append(f"高频保持段平均频率 {avg_f:.4f} Hz ≠ 50.30 Hz")

    # 检查 7: 斜坡端点频率
    # 15s 时刻接近 50.00
    t15 = [r for r in rows if abs(r["timestamp_s"] - 15.0) < DT_S / 2]
    if t15:
        f15 = t15[0]["grid_frequency_hz"]
        if abs(f15 - 50.00) > TOL:
            errors.append(f"t=15s 频率 {f15:.4f} ≠ 50.00")
        # RoCoF at boundary should be the ramp value or 0 (boundary points may differ by one step)
        # We allow either since the boundary could belong to either segment

    # 30s 时刻接近 49.80
    t30 = [r for r in rows if abs(r["timestamp_s"] - 30.0) < DT_S / 2]
    if t30:
        f30 = t30[0]["grid_frequency_hz"]
        if abs(f30 - 49.80) > TOL:
            errors.append(f"t=30s 频率 {f30:.4f} ≠ 49.80")

    # 检查 8: 计划功率恒定
    for r in rows:
        if abs(r["plan_power_mw"] - PLAN_STEADY_MW) > 0.001:
            errors.append(f"t={r['timestamp_s']}s: 计划功率偏离 {r['plan_power_mw']}")
            break

    # 检查 9: 通讯状态始终为 1.0
    for r in rows:
        if r["comm_status"] != 1.0:
            errors.append(f"t={r['timestamp_s']}s: 通讯异常 comm_status={r['comm_status']}")
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
    freqs = [r["grid_frequency_hz"] for r in rows]
    rocofs = [abs(r["rocof_hz_per_s"]) for r in rows]
    print(f"\n  频率范围: [{min(freqs):.3f}, {max(freqs):.3f}] Hz")
    print(f"  频率偏差范围: [{min(freqs)-NOMINAL_FREQ_HZ:+.3f}, "
          f"{max(freqs)-NOMINAL_FREQ_HZ:+.3f}] Hz")
    print(f"  最大 |RoCoF|: {max(rocofs):.4f} Hz/s")
    print(f"  PFR 死区: ±0.05 Hz (0~15s 频率在死区内运行)")
    print(f"  计划功率: {PLAN_STEADY_MW} MW 恒定")


def main():
    parser = argparse.ArgumentParser(
        description="场景4 (S4) PFR 一次调频测试数据生成器",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=f"""
示例:
  python s4_pfr_generator.py
  python s4_pfr_generator.py --output ../test_cases/s4_custom.csv
  python s4_pfr_generator.py --seed 12345
        """
    )
    parser.add_argument("--output", "-o", default=None,
                        help="输出 CSV 路径 (默认: ../../test_cases/s4_pfr/s4_pfr_test.csv)")
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
        out_path = os.path.join(script_dir, "..", "..", "test_cases", "s4_pfr", "s4_pfr_test.csv")
    out_path = os.path.normpath(out_path)

    print(f"S4 PFR 一次调频测试数据生成器 v1.0")
    print(f"  总时长:   {DURATION_S} s")
    print(f"  采样步长: {DT_S*1000:.0f} ms")
    print(f"  频率剖面: "
          f"50.00(0~15s) → 49.93(15.5s 阶跃) → 49.80(30~45s) → "
          f"50.00(55s) → 50.30(70~80s) → 50.00(90s)")
    print(f"  额定频率: {NOMINAL_FREQ_HZ} Hz")
    print(f"  计划功率: {PLAN_STEADY_MW} MW")
    print(f"  输出路径: {out_path}")
    print()

    rows = generate_sequence()
    write_csv(rows, out_path)

    if not args.no_check:
        self_check(rows)

    return 0


if __name__ == "__main__":
    sys.exit(main())
