#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ============================================================
# s3_step_generator.py — S3 调度阶跃测试 v3.0
# v3.0: 上下行分离+PV预定位+S1恢复, 140s, 上下行各35s执行+25s恢复
# ============================================================
import csv, math, os, random, sys
random.seed(42)

DT_S       = 0.1
DURATION_S = 140.0
NUM_STEPS  = int(DURATION_S / DT_S) + 1

PLANT_MW   = 120.0
PLAN_INIT  = 80.0
PLAN_UP    = 110.0
PLAN_DN    = 50.0
NUM_PV     = 3

T_UP_STEP    = 20.0   # 上行阶跃时刻
T_UP_PV_PRE  = 18.0   # PV预定位开始
T_DN_STEP    = 80.0   # 下行阶跃时刻
T_DN_PV_PRE  = 78.0   # PV预定位开始

def generate():
    rows = []
    for i in range(NUM_STEPS):
        t = i * DT_S

        # 计划: 阶跃信号
        if t < T_UP_STEP:        plan = PLAN_INIT
        elif t < T_DN_STEP:      plan = PLAN_UP
        else:                    plan = PLAN_DN

        # PV: 预定位避免 ratio<0.85 触发S5
        if t < T_UP_PV_PRE:
            pv = PLAN_INIT  # 上行前: PV=80MW
        elif t < T_UP_STEP + 0.5:
            pv = 105.0       # 上行预定位: PV=105 (plan=110, ratio=0.95)
        elif t < T_DN_PV_PRE:
            pv = 105.0       # 上行后稳态
        elif t < T_DN_STEP + 0.5:
            pv = 55.0        # 下行预定位: PV=55 (plan=50, ratio=0.91)
        else:
            pv = 55.0        # 下行后稳态

        pv_per = pv / NUM_PV
        freq_noise = max(-0.02, min(0.02, random.gauss(0, 0.008)))
        volt_noise = max(-0.005, min(0.005, random.gauss(0, 0.002)))
        soc = 60.0 + 0.02 * math.sin(2 * math.pi * 0.01 * t)
        irr = max(400.0, min(1000.0, 1000.0 * pv / PLANT_MW))

        rows.append({
            "timestamp_s": round(t, 3), "grid_frequency_hz": round(50.0+freq_noise,4),
            "grid_voltage_pu": round(1.0+volt_noise,4), "rocof_hz_per_s": 0.0,
            "freq_deviation_hz": round(-freq_noise,4),
            "pv_total_power_mw": round(pv,4), "pv_inv1_power_mw": round(pv_per,4),
            "pv_inv2_power_mw": round(pv_per,4), "pv_inv3_power_mw": round(pv_per,4),
            "plan_power_mw": float(plan), "irradiance_w_per_m2": round(irr,2),
            "ess_soc_pct": round(soc,4), "ess_power_mw": 0.0, "comm_status": 1.0,
        })

    pvs = [r["pv_total_power_mw"] for r in rows]
    plans = set(r["plan_power_mw"] for r in rows)
    print(f"[S3 GEN] {len(rows)} rows | plans={sorted(plans)}MW | PV range {min(pvs):.0f}-{max(pvs):.0f}MW")
    return rows

def write_csv(rows, path):
    fields = ["timestamp_s","grid_frequency_hz","grid_voltage_pu","rocof_hz_per_s",
              "freq_deviation_hz","pv_total_power_mw","pv_inv1_power_mw",
              "pv_inv2_power_mw","pv_inv3_power_mw","plan_power_mw",
              "irradiance_w_per_m2","ess_soc_pct","ess_power_mw","comm_status"]
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fields); w.writeheader(); w.writerows(rows)
    print(f"[S3 GEN] → {path}")

if __name__ == "__main__":
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "..", "..", "test_cases", "s3_step", "s3_step_test.csv")
    write_csv(generate(), os.path.normpath(out))
