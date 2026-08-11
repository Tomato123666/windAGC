#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ============================================================
# wind_agc_generator.py — 风电AGC全场景测试数据生成器
# ============================================================
# 版本: v1.0  |  日期: 2026-08-04
# ============================================================
# 生成6个场景的测试CSV, 用于驱动 unified_agc.exe 进行自动化测试.
#
# 每个场景输出一个 CSV 文件, 包含完整的时间序列:
#   timestamp_s     - 仿真时间 (秒)
#   grid_frequency_hz   - 电网频率 (Hz)
#   wind_speed_ms       - 全场平均风速 (m/s)
#   turbulence          - 湍流强度
#   dispatch_target_mw  - 调度目标功率 (MW)
#   dispatch_type       - 指令类型 (0=无指令, 1=STEP, 2=RAMP)
#   ramp_rate_mw_min    - 爬坡速率 (MW/min)
#   comm_status         - 通信状态 (1.0=正常, 0.0=中断)
#   extreme_type        - 极端风况 (0=无, 1=切出, 2=高湍流, 3=风暴穿越)
#   curtail_ratio       - 限电比例 (0~1)
#   schedule_power_mw   - 调度计划功率 (MW)
#
# 使用:
#   python generator.py --scene 1                    # 生成场景1
#   python generator.py --scene all                  # 生成全部6个场景
#   python generator.py --scene 4 --duration 120     # 场景4, 120秒
#   python generator.py --scene all --seed 123       # 全部场景, 指定种子
# ============================================================

import argparse
import csv
import math
import os
import random
import sys

# ============================================================
# 全局参数 — 与 unified_agc 保持一致
# ============================================================
NOMINAL_FREQ_HZ    = 50.00       # 额定频率 (Hz)
TURBINE_COUNT      = 10          # 风机数量
TURBINE_RATED_MW   = 3.0         # 单台额定 (MW)
TOTAL_RATED_MW     = 30.0        # 全场额定 (MW)
CUT_OUT_SPEED      = 25.0        # 切出风速 (m/s)
HIGH_TURB_THRESH   = 0.25        # 高湍流阈值
PFR_DEADBAND_HZ    = 0.10        # 一次调频死区 (Hz)
CURTAIL_THRESHOLD  = 0.40        # 限电触发阈值
RAMP_DETECT_MW     = 5.0         # 爬坡检测阈值 (MW)
DEFAULT_DT_S       = 0.1         # 默认采样步长 (s) = 100ms
OUTPUT_BASE_DIR    = "output"


def clamp(v, lo, hi):
    return max(lo, min(hi, v))


def make_header():
    return [
        "timestamp_s", "grid_frequency_hz", "wind_speed_ms",
        "turbulence", "dispatch_target_mw", "dispatch_type",
        "ramp_rate_mw_min", "comm_status", "extreme_type",
        "curtail_ratio", "schedule_power_mw"
    ]


# ============================================================
# 场景1: 常规AGC稳态跟踪 (S1 Baseline)
# ============================================================
def gen_s1(duration: float, dt: float) -> list[dict]:
    """稳态计划跟踪: 频率正常, 风速稳定, 调度指令缓慢变化"""
    rows = []
    steps = int(duration / dt) + 1
    for i in range(steps):
        t = i * dt
        freq    = NOMINAL_FREQ_HZ + clamp(random.gauss(0, 0.008), -0.02, 0.02)
        wind    = 12.0 + 0.5 * math.sin(2 * math.pi * 0.01 * t)
        turb    = 0.05 + clamp(random.gauss(0, 0.01), -0.02, 0.02)
        target  = 15.0 + 3.0 * math.sin(2 * math.pi * t / 600.0)
        rows.append({
            "timestamp_s": round(t, 3), "grid_frequency_hz": round(freq, 4),
            "wind_speed_ms": round(wind, 2), "turbulence": round(turb, 4),
            "dispatch_target_mw": round(target, 2), "dispatch_type": 0,
            "ramp_rate_mw_min": 0.0, "comm_status": 1.0, "extreme_type": 0,
            "curtail_ratio": 0.0, "schedule_power_mw": round(target, 2),
        })
    return rows


# ============================================================
# 场景2: 风速波动抑制 (S2 Wind Disturbance)
# ============================================================
def gen_s2(duration: float, dt: float) -> list[dict]:
    """风速突变: 12→15m/s 突增, 触发波动抑制"""
    rows = []
    steps = int(duration / dt) + 1
    for i in range(steps):
        t = i * dt
        freq = NOMINAL_FREQ_HZ + clamp(random.gauss(0, 0.008), -0.02, 0.02)
        # 0-20s: 12m/s, 20-60s: 线性增至15m/s, 60-80s: 保持15, 80-120s: 线性降回12
        if t < 20:
            wind = 12.0
        elif t < 60:
            wind = 12.0 + (t - 20) / 40.0 * 3.0
        elif t < 80:
            wind = 15.0
        elif t < 120:
            wind = 15.0 - (t - 80) / 40.0 * 3.0
        else:
            wind = 12.0
        wind += clamp(random.gauss(0, 0.3), -0.5, 0.5)
        turb = 0.05 + (wind - 12.0) * 0.01
        target = 15.0
        rows.append({
            "timestamp_s": round(t, 3), "grid_frequency_hz": round(freq, 4),
            "wind_speed_ms": round(wind, 2), "turbulence": round(turb, 4),
            "dispatch_target_mw": round(target, 2), "dispatch_type": 0,
            "ramp_rate_mw_min": 0.0, "comm_status": 1.0, "extreme_type": 0,
            "curtail_ratio": 0.0, "schedule_power_mw": round(target, 2),
        })
    return rows


# ============================================================
# 场景3: 一次调频 (S3 Frequency Regulation)
# ============================================================
def gen_s3(duration: float, dt: float) -> list[dict]:
    """频率扰动: 49.7Hz 低频事件, 触发一次调频"""
    rows = []
    steps = int(duration / dt) + 1
    for i in range(steps):
        t = i * dt
        # 0-10s: 50Hz 正常, 10-60s: 49.7Hz, 60-90s: 渐变恢复50Hz
        if t < 10:
            freq = 50.00
        elif t < 60:
            freq = 49.70
        elif t < 90:
            freq = 49.70 + (t - 60) / 30.0 * 0.30
        else:
            freq = 50.00
        freq += clamp(random.gauss(0, 0.005), -0.01, 0.01)
        wind = 12.0 + clamp(random.gauss(0, 0.2), -0.5, 0.5)
        turb = 0.05
        rows.append({
            "timestamp_s": round(t, 3), "grid_frequency_hz": round(freq, 4),
            "wind_speed_ms": round(wind, 2), "turbulence": round(turb, 4),
            "dispatch_target_mw": 15.0, "dispatch_type": 0,
            "ramp_rate_mw_min": 0.0, "comm_status": 1.0, "extreme_type": 0,
            "curtail_ratio": 0.0, "schedule_power_mw": 15.0,
        })
    return rows


# ============================================================
# 场景4: 调度指令爬坡跟踪 (S4 Ramp Tracking)
# ============================================================
def gen_s4(duration: float, dt: float) -> list[dict]:
    """调度阶跃: 15→25MW STEP 指令"""
    rows = []
    steps = int(duration / dt) + 1
    for i in range(steps):
        t = i * dt
        freq = NOMINAL_FREQ_HZ + clamp(random.gauss(0, 0.008), -0.02, 0.02)
        wind = 12.0 + clamp(random.gauss(0, 0.2), -0.5, 0.5)
        turb = 0.05
        # 0-10s: 15MW STEP, 10-30s: 15→25MW RAMP 5MW/min
        if t < 10:
            target, dtype, ramp = 15.0, 1, 0.0
        elif t < 30:
            target, dtype, ramp = 25.0, 2, 5.0
        else:
            target, dtype, ramp = 25.0, 0, 0.0
        rows.append({
            "timestamp_s": round(t, 3), "grid_frequency_hz": round(freq, 4),
            "wind_speed_ms": round(wind, 2), "turbulence": round(turb, 4),
            "dispatch_target_mw": round(target, 2), "dispatch_type": dtype,
            "ramp_rate_mw_min": round(ramp, 2), "comm_status": 1.0,
            "extreme_type": 0, "curtail_ratio": 0.0,
            "schedule_power_mw": round(target, 2),
        })
    return rows


# ============================================================
# 场景5: 限电管理 (S5 Curtailment)
# ============================================================
def gen_s5(duration: float, dt: float) -> list[dict]:
    """限电工况: 高风速→高可用功率, 调度要求限电"""
    rows = []
    steps = int(duration / dt) + 1
    for i in range(steps):
        t = i * dt
        freq = NOMINAL_FREQ_HZ + clamp(random.gauss(0, 0.008), -0.02, 0.02)
        # 高风速: 14~16m/s → 可用功率高
        wind = 14.0 + 2.0 * math.sin(2 * math.pi * t / 200.0)
        turb = 0.08
        # 0-30s: 正常15MW,  30-90s: 限电10MW (限电比例>40%),
        # 90-120s: 恢复15MW
        if t < 30:
            target, ratio = 15.0, 0.0
        elif t < 90:
            target, ratio = 10.0, 0.55
        else:
            target, ratio = 15.0, 0.0
        rows.append({
            "timestamp_s": round(t, 3), "grid_frequency_hz": round(freq, 4),
            "wind_speed_ms": round(wind, 2), "turbulence": round(turb, 4),
            "dispatch_target_mw": round(target, 2), "dispatch_type": 0,
            "ramp_rate_mw_min": 0.0, "comm_status": 1.0, "extreme_type": 0,
            "curtail_ratio": round(ratio, 4), "schedule_power_mw": round(target, 2),
        })
    return rows


# ============================================================
# 场景6: 通信中断与极端风况 (S6 Safety)
# ============================================================
def gen_s6(duration: float, dt: float) -> list[dict]:
    """安全模式: 通信中断 + 极端风况, 触发安全保护"""
    rows = []
    steps = int(duration / dt) + 1
    for i in range(steps):
        t = i * dt
        freq = NOMINAL_FREQ_HZ + clamp(random.gauss(0, 0.008), -0.02, 0.02)
        # 0-20s: 正常, 20-40s: 通信中断,
        # 40-70s: 正常, 70-100s: 极端风况26m/s切出, 100-120s: 恢复
        if t < 20:
            wind, comm, ext = 12.0, 1.0, 0
        elif t < 40:
            wind, comm, ext = 12.5, 0.0, 0  # 通信中断
        elif t < 70:
            wind, comm, ext = 13.0, 1.0, 0  # 恢复
        elif t < 100:
            wind, comm, ext = 26.0, 1.0, 1  # 切出风速
        else:
            wind, comm, ext = 12.0, 1.0, 0  # 恢复
        turb = 0.05 if ext == 0 else 0.30
        rows.append({
            "timestamp_s": round(t, 3), "grid_frequency_hz": round(freq, 4),
            "wind_speed_ms": round(wind, 2), "turbulence": round(turb, 4),
            "dispatch_target_mw": 15.0, "dispatch_type": 0,
            "ramp_rate_mw_min": 0.0, "comm_status": comm,
            "extreme_type": ext, "curtail_ratio": 0.0,
            "schedule_power_mw": 15.0,
        })
    return rows


# ============================================================
# 24小时综合工况 (S7 Combined)
# ============================================================
def gen_s7_combined(duration: float, dt: float) -> list[dict]:
    """24小时综合: 覆盖全部6个场景的时序剖面, 对齐 unified_agc/main.cpp"""
    rows = []
    steps = int(duration / dt) + 1
    for i in range(steps):
        t = i * dt
        hour = t / 3600.0
        freq = NOMINAL_FREQ_HZ

        # ---- 与 unified_agc/main.cpp 的工况脚本对齐 ----
        # 0-2h: 常规AGC, 15MW
        if hour < 2.0:
            wind, turb, target, dtype, ramp = 12.0, 0.05, 15.0, 0, 0.0
            comm, ext, ratio = 1.0, 0, 0.0
        # 2-3h: 风速突增 12→15m/s
        elif hour < 3.0:
            p = (hour - 2.0)
            wind = 12.0 + p * 3.0
            turb, target, dtype, ramp = 0.10, 15.0, 0, 0.0
            comm, ext, ratio = 1.0, 0, 0.0
        # 3-3.5h: 通信中断
        elif hour < 3.5:
            wind, turb, target, dtype, ramp = 12.0, 0.05, 15.0, 0, 0.0
            comm, ext, ratio = 0.0, 0, 0.0
        # 3.5-5h: 通信恢复
        elif hour < 5.0:
            wind, turb, target, dtype, ramp = 12.0, 0.05, 15.0, 0, 0.0
            comm, ext, ratio = 1.0, 0, 0.0
        # 5-6h: 频率扰动 49.7Hz
        elif hour < 6.0:
            wind, turb, target, dtype, ramp = 12.0, 0.05, 15.0, 0, 0.0
            comm, ext, ratio = 1.0, 0, 0.0
            if hour < 5.01:
                freq = 49.70
            else:
                freq = 49.70 + (hour - 5.01) / 0.99 * 0.30  # gradual recovery
        # 6-8h: 调度阶跃 15→25MW STEP
        elif hour < 8.0:
            wind, turb, target, dtype, ramp = 12.0, 0.05, 25.0, 1, 0.0
            comm, ext, ratio = 1.0, 0, 0.0
        # 8-12h: 限电管理 (高风速 + 高限电比例)
        elif hour < 12.0:
            wind, turb, target, dtype, ramp = 14.0, 0.08, 15.0, 0, 0.0
            comm, ext, ratio = 1.0, 0, 0.50
        # 12-22h: 常规AGC, 20MW
        elif hour < 22.0:
            wind, turb, target, dtype, ramp = 12.0, 0.05, 20.0, 0, 0.0
            comm, ext, ratio = 1.0, 0, 0.0
        # 22-23h: 极端风况 26m/s 切出
        elif hour < 23.0:
            wind, turb, target, dtype, ramp = 26.0, 0.10, 20.0, 0, 0.0
            comm, ext, ratio = 1.0, 1, 0.0
        # 23-24h: 恢复
        else:
            wind, turb, target, dtype, ramp = 10.0, 0.05, 15.0, 0, 0.0
            comm, ext, ratio = 1.0, 0, 0.0

        freq += clamp(random.gauss(0, 0.005), -0.01, 0.01)
        wind += clamp(random.gauss(0, 0.2), -0.5, 0.5)

        rows.append({
            "timestamp_s": round(t, 3), "grid_frequency_hz": round(freq, 4),
            "wind_speed_ms": round(wind, 2), "turbulence": round(turb, 4),
            "dispatch_target_mw": round(target, 2), "dispatch_type": dtype,
            "ramp_rate_mw_min": round(ramp, 2), "comm_status": comm,
            "extreme_type": ext, "curtail_ratio": round(ratio, 4),
            "schedule_power_mw": round(target, 2),
        })
    return rows


# ============================================================
# 场景注册表
# ============================================================
SCENES = {
    "1":   ("s1_baseline",        gen_s1,   60.0,   "稳态计划跟踪"),
    "2":   ("s2_wind_disturbance", gen_s2,  120.0,  "风速波动抑制"),
    "3":   ("s3_freq_regulation", gen_s3,   90.0,   "一次调频"),
    "4":   ("s4_ramp_tracking",   gen_s4,   60.0,   "调度指令爬坡跟踪"),
    "5":   ("s5_curtailment",     gen_s5,  120.0,   "限电管理"),
    "6":   ("s6_safety",          gen_s6,  120.0,   "通信中断与安全模式"),
    "7":   ("s7_24h_combined",    gen_s7_combined, 86400.0, "24小时综合工况"),
    "all": ("all", None, None, ""),
}


def write_csv(rows: list[dict], filepath: str):
    os.makedirs(os.path.dirname(filepath) or ".", exist_ok=True)
    with open(filepath, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=make_header())
        w.writeheader()
        w.writerows(rows)
    print(f"  [OK] {len(rows)} rows → {filepath}")


def self_check(rows: list[dict], scene_id: str):
    """验证数据合理性"""
    errors = []
    for r in rows:
        t, f, ws = r["timestamp_s"], r["grid_frequency_hz"], r["wind_speed_ms"]
        # 频率合法范围
        if f < 49.0 or f > 51.0:
            errors.append(f"t={t}s: freq={f:.2f}Hz out of [49,51]")
        # 风速合法范围
        if ws < 0 or ws > 40:
            errors.append(f"t={t}s: wind={ws:.1f}m/s out of [0,40]")
        # 目标功率合法范围
        if r["dispatch_target_mw"] < 0:
            errors.append(f"t={t}s: target={r['dispatch_target_mw']} < 0")
        # 通信状态
        if r["comm_status"] not in (0.0, 1.0):
            errors.append(f"t={t}s: comm={r['comm_status']}")
        # 极端类型
        if r["extreme_type"] not in (0, 1, 2, 3):
            errors.append(f"t={t}s: extreme_type={r['extreme_type']}")
        # 限电比例
        if r["curtail_ratio"] < 0 or r["curtail_ratio"] > 1:
            errors.append(f"t={t}s: curtail_ratio={r['curtail_ratio']}")

    if errors:
        print(f"  [WARN] {len(errors)} issues:")
        for e in errors[:8]:
            print(f"    ! {e}")
    else:
        print(f"  [OK] Self-check passed — all constraints satisfied")

    # Statistics
    freqs = [r["grid_frequency_hz"] for r in rows]
    winds = [r["wind_speed_ms"] for r in rows]
    scenes_changed = 0
    prev_dtype = -1
    for r in rows:
        if r["dispatch_type"] != prev_dtype:
            scenes_changed += 1
            prev_dtype = r["dispatch_type"]
    print(f"  freq: [{min(freqs):.3f}, {max(freqs):.3f}] Hz  "
          f"wind: [{min(winds):.1f}, {max(winds):.1f}] m/s  "
          f"cmd changes: {scenes_changed}")


def main():
    parser = argparse.ArgumentParser(
        description="风电AGC全场景测试数据生成器 v1.0",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  python generator.py --scene 1
  python generator.py --scene all
  python generator.py --scene 4 --duration 120 --step 0.5
  python generator.py --scene 7 --step 60.0     # 24h综合, 1min步长
  python generator.py --scene all --seed 12345 --output ../test_cases
        """
    )
    parser.add_argument("--scene", "-s", required=True,
                        choices=["1","2","3","4","5","6","7","all"],
                        help="场景编号 (1-7) 或 all")
    parser.add_argument("--duration", "-d", type=float, default=None,
                        help="自定义时长 (秒), 覆盖默认值")
    parser.add_argument("--step", type=float, default=DEFAULT_DT_S,
                        help=f"采样步长 (秒), 默认 {DEFAULT_DT_S}s")
    parser.add_argument("--seed", type=int, default=42,
                        help="随机种子 (默认 42)")
    parser.add_argument("--output", "-o", default=None,
                        help=f"输出目录 (默认: {OUTPUT_BASE_DIR}/)")
    parser.add_argument("--no-check", action="store_true",
                        help="跳过后置自检")
    args = parser.parse_args()

    random.seed(args.seed)
    out_base = args.output or OUTPUT_BASE_DIR

    scene_ids = ["1","2","3","4","5","6","7"] if args.scene == "all" else [args.scene]

    print(f"风电AGC数据生成器 v1.0 | seed={args.seed} | dt={args.step}s\n")

    for sid in scene_ids:
        name, gen_fn, default_dur, desc = SCENES[sid]
        duration = args.duration if args.duration else default_dur
        out_path = os.path.join(out_base, f"{name}.csv")
        out_path = os.path.normpath(out_path)

        print(f"[S{sid}] {desc} ({duration}s)")
        rows = gen_fn(duration, args.step)
        write_csv(rows, out_path)
        if not args.no_check:
            self_check(rows, sid)
        print()

    print(f"Done. {len(scene_ids)} scenario(s) generated → {os.path.abspath(out_base)}/")
    return 0


if __name__ == "__main__":
    sys.exit(main())
