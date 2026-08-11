#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ============================================================
# s6_comms_generator.py — S6 通信中断三策略测试数据生成器 v3.0
# ============================================================
# --test s6-1/s6-2/s6-3  单策略生成
# --all                   三策略一键生成
# ★ 首行(t=0s) plan=策略号(1/2/3) → Runner 据此写 AGC.S6_STRATEGY
# ★ 第2行起(t≥0.1s) plan=正常业务值
# ============================================================

import argparse, csv, math, os, random, sys

random.seed(42)

DT_S = 0.1; NOMINAL_FREQ = 50.0; NOMINAL_VOLTAGE = 1.0
FREQ_NOISE_SIGMA = 0.002; FREQ_CLIP = 0.005
VOLT_NOISE_SIGMA = 0.002; VOLT_CLIP = 0.005
IRRADIANCE_BASE = 800.0; ESS_SOC_INITIAL = 60.0; NUM_PV = 3

def clamp(v, lo, hi): return max(lo, min(hi, v))

def make_row(t, freq, v_noise, pv_total, plan, comm):
    pv_per = pv_total / NUM_PV
    irrad = clamp(IRRADIANCE_BASE * (pv_total / 100.0), 200.0, 1000.0)
    soc = ESS_SOC_INITIAL + 0.01 * math.sin(2 * math.pi * 0.005 * t)
    return {
        "timestamp_s": round(t, 3), "grid_frequency_hz": round(freq, 4),
        "grid_voltage_pu": round(NOMINAL_VOLTAGE + v_noise, 4),
        "rocof_hz_per_s": 0.0,
        "freq_deviation_hz": round(NOMINAL_FREQ - freq, 4),
        "pv_total_power_mw": round(pv_total, 4),
        "pv_inv1_power_mw": round(pv_per, 4),
        "pv_inv2_power_mw": round(pv_per, 4),
        "pv_inv3_power_mw": round(pv_per, 4),
        "plan_power_mw": round(plan, 4),
        "irradiance_w_per_m2": round(irrad, 2),
        "ess_soc_pct": round(soc, 4), "ess_power_mw": 0.0,
        "comm_status": comm,
    }

def gen_noise(n):
    return ([clamp(random.gauss(0, FREQ_NOISE_SIGMA), -FREQ_CLIP, FREQ_CLIP) for _ in range(n)],
            [clamp(random.gauss(0, VOLT_NOISE_SIGMA), -VOLT_CLIP, VOLT_CLIP) for _ in range(n)])

# ═══ S6-1: HLV — plan 全程 90MW(中断前)→0(中断后, HLV 锁死 90MW) ═══
def gen_s6_1():
    DUR = 50.0; N = int(DUR / DT_S) + 1; rows = []  # ★ v4.34: 50s, 30s恢复后20s退出窗口
    fn, vn = gen_noise(N)
    for i in range(N):
        t = i * DT_S
        comm = 0.0 if (5.0 <= t < 30.0) else 1.0  # 通信在30s恢复, 留20s做退出
        plan = 90.0 if t < 5.0 else 0.0
        pv  = 90.0 if t < 5.0 else (90.0 + 10.0 * math.sin(2 * math.pi * 0.05 * t))
        rows.append(make_row(t, NOMINAL_FREQ + fn[i], vn[i], pv, plan, comm))
    return rows, DUR

# ═══ S6-2: 辐照度波动 + ESS 充电验证 ═══
# ★ v4.34: 加入正弦辐照度波动 (600~1000 W/m², 周期~120s),
#   PV 出力相应波动 (72~120MW), 当 PV>负荷(100MW)时 ESS 充电,
#   当 PV<负荷时不放电(策略2仅充电), 从 SOC 变化可验证控制闭环
#   comm 在 50s 恢复 (给 20s 做 S6→S1 退出+确认)
def gen_s6_2():
    DUR = 90.0; N = int(DUR / DT_S) + 1; rows = []  # ★ v4.34: 90s, 50s恢复后40s退出窗口
    fn, vn = gen_noise(N)
    for i in range(N):
        t = i * DT_S; comm = 0.0 if (5.0 <= t < 50.0) else 1.0
        # 辐照度正弦波动: 800±200 W/m², 周期 120s
        irr_var = 200.0 * math.sin(2 * math.pi * t / 120.0)
        irradiance = clamp(800.0 + irr_var, 600.0, 1000.0)
        # PV 出力 = 120MW × (irradiance/1000)
        pv_total = 120.0 * (irradiance / 1000.0)
        rows.append(make_row(t, NOMINAL_FREQ + fn[i], vn[i], pv_total, 100.0, comm))
    return rows, DUR

# ═══ S6-3: Droop+MPPT — plan 全程 80MW, f=49.80Hz ═══
def gen_s6_3():
    DUR = 45.0; N = int(DUR / DT_S) + 1; rows = []  # ★ v4.34: 45s, 28s恢复后17s退出窗口
    _, vn = gen_noise(N)
    for i in range(N):
        t = i * DT_S; comm = 0.0 if (5.0 <= t < 28.0) else 1.0; plan = 80.0
        if t < 5.0:       freq = NOMINAL_FREQ
        elif t < 7.0:     freq = NOMINAL_FREQ - (t - 5.0) / 2.0 * 0.20
        elif t < 25.0:    freq = 49.80
        elif t < 28.0:    freq = 49.80 + (t - 25.0) / 3.0 * 0.20
        else:             freq = NOMINAL_FREQ
        pv = 80.0 if t < 5.0 else min(80.0 + (t - 5.0) / 5.0 * 20.0, 100.0)
        rows.append(make_row(t, freq, vn[i], pv, plan, comm))
    return rows, DUR


def write_csv(rows, filepath, strategy_id):
    fields = ["timestamp_s","grid_frequency_hz","grid_voltage_pu","rocof_hz_per_s",
              "freq_deviation_hz","pv_total_power_mw","pv_inv1_power_mw",
              "pv_inv2_power_mw","pv_inv3_power_mw","plan_power_mw",
              "irradiance_w_per_m2","ess_soc_pct","ess_power_mw","comm_status"]
    os.makedirs(os.path.dirname(filepath) or ".", exist_ok=True)
    with open(filepath, "w", newline="", encoding="utf-8") as f:
        f.write(f"# S6_STRATEGY={strategy_id}\n")  # ★ 策略注释行 (Runner 解析)
        w = csv.DictWriter(f, fieldnames=fields); w.writeheader(); w.writerows(rows)
    print(f"[OK] {len(rows)} 行 → {filepath}")


def main():
    p = argparse.ArgumentParser(description="S6 通信中断三策略测试数据生成器 v3.0")
    p.add_argument("--test","-t",default=None,choices=["s6-1","s6-2","s6-3"])
    p.add_argument("--all","-a",action="store_true",help="一键生成三策略 CSV")
    p.add_argument("--output-dir","-o",default=None,help="输出目录")
    p.add_argument("--seed","-s",type=int,default=None)
    args = p.parse_args()
    if args.seed is not None: random.seed(args.seed)

    sd = os.path.dirname(os.path.abspath(__file__))
    out_dir = args.output_dir or os.path.join(sd, "..", "..", "test_cases", "s6_comms")

    tests = {
        "s6-1": (gen_s6_1, "s6_1_hlv.csv",   "策略一 HLV (Hold Last Value)"),
        "s6-2": (gen_s6_2, "s6_2_ramp.csv",  "策略二 Safe Ramp-Down"),
        "s6-3": (gen_s6_3, "s6_3_droop.csv", "策略三 Local Droop + MPPT"),
    }

    run_list = list(tests.keys()) if args.all else ([args.test] if args.test else ["s6-1"])

    for key in run_list:
        gen_fn, fname, desc = tests[key]
        rows, dur = gen_fn()
        strategy_id = int(key.split('-')[1])  # "s6-1" → 1
        out = os.path.join(out_dir, fname)
        print(f"[{key}] {desc}: {dur}s, {len(rows)}行")
        write_csv(rows, os.path.normpath(out), strategy_id)

    if args.all:
        print(f"\n[S6] 三策略 CSV 已生成 → {os.path.normpath(out_dir)}")

if __name__ == "__main__": main()
