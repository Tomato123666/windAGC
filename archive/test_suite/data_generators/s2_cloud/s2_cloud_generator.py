#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ============================================================
# s2_cloud_generator.py — 场景2 云遮测试数据生成器 v2.0
# ============================================================
# v2.0: 云边缘效应 + 有功减载裕度 (符合工业物理规律)
#   - 云遮下降: 840→200 W/m² 渐变15s (速率43 W/m²/s)
#   - 云散恢复: 200→950 W/m² 渐变15s (速率50 W/m²/s)
#   - 恢复期PV>计划: 14MW盈余→ESS回充
# ============================================================

import csv, math, os, random, sys

random.seed(42)

# ── 时间参数 (★ v4.35: 120s, S1 30s收敛 + 恢复35s充电窗) ──
DT_S         = 0.1
DURATION_S   = 120.0
NUM_STEPS    = int(DURATION_S / DT_S) + 1

# ── 辐照度剖面 ──
IRR_PRE      = 833.0   # 云遮前 (PV≈100MW ≡ plan)
IRR_VALLEY   = 200.0   # 谷底 (PV≈24MW, 缺口76MW)
IRR_POST     = 1050.0  # 云散后 (PV≈126MW, 26MW盈余→ESS回充)
RAMP_DN_S    = 30.0    # 下降开始 (S1收敛30s后)
RAMP_DN_E    = 45.0    # 下降结束 (15s渐变)
VALLEY_E     = 60.0    # 谷底结束 (15s)
RAMP_UP_S    = 60.0    # 恢复开始
RAMP_UP_E    = 75.0    # 恢复结束 (15s渐变)

# ── 电站参数 ──
PLANT_MW     = 120.0
PLAN_MW      = 100.0   # SCADA计划 (留20MW减载裕度)
NUM_PV       = 3
SOC_INIT     = 60.0
FREQ_NOM     = 50.0
VOLT_NOM     = 1.0

def irradiance(t):
    """辐照度剖面: 斜坡渐变 (云边缘效应)"""
    # ★ v4.35: 0-3s暖机斜坡 800→833, 匹配AGC冷启动默认辐照度, 消除RESET伪阶跃
    if t < 3.0:
        frac = t / 3.0
        return 800.0 + frac * (IRR_PRE - 800.0)
    elif t < RAMP_DN_S:
        return IRR_PRE + gauss_noise(t, 10.0, 30.0)
    elif t < RAMP_DN_E:
        frac = (t - RAMP_DN_S) / (RAMP_DN_E - RAMP_DN_S)
        return IRR_PRE + frac * (IRR_VALLEY - IRR_PRE)  # 斜坡段无噪声
    elif t < VALLEY_E:
        return IRR_VALLEY + gauss_noise(t, 3.3, 10.0)
    elif t < RAMP_UP_E:
        frac = (t - RAMP_UP_S) / (RAMP_UP_E - RAMP_UP_S)
        return IRR_VALLEY + frac * (IRR_POST - IRR_VALLEY)  # 斜坡段无噪声
    else:
        return IRR_POST + gauss_noise(t, 10.0, 30.0)

def gauss_noise(t, sigma, clip):
    """基于时间的确定性噪声 (种子固定, 可复现)"""
    random.seed(int(t * 1000) + 42)
    v = random.gauss(0, sigma)
    return max(-clip, min(clip, v))

def generate():
    rows = []
    for i in range(NUM_STEPS):
        t = i * DT_S
        irr = max(0.0, min(1200.0, irradiance(t)))
        pv  = PLANT_MW * (irr / 1000.0)
        pv_per = pv / NUM_PV

        freq_noise = max(-0.02, min(0.02, random.gauss(0, 0.008)))
        volt_noise = max(-0.005, min(0.005, random.gauss(0, 0.002)))
        soc = SOC_INIT + 0.02 * math.sin(2 * math.pi * 0.01 * t)

        rows.append({
            "timestamp_s":          round(t, 3),
            "grid_frequency_hz":    round(FREQ_NOM + freq_noise, 4),
            "grid_voltage_pu":      round(VOLT_NOM + volt_noise, 4),
            "rocof_hz_per_s":       0.0,
            "freq_deviation_hz":    round(-freq_noise, 4),
            "pv_total_power_mw":    round(pv, 4),
            "pv_inv1_power_mw":     round(pv_per, 4),
            "pv_inv2_power_mw":     round(pv_per, 4),
            "pv_inv3_power_mw":     round(pv_per, 4),
            "plan_power_mw":        float(PLAN_MW),
            "irradiance_w_per_m2":  round(irr, 2),
            "ess_soc_pct":          round(soc, 4),
            "ess_power_mw":         0.0,
            "comm_status":          1.0,
        })

    # 自检
    irrs = [r["irradiance_w_per_m2"] for r in rows]
    pvs  = [r["pv_total_power_mw"] for r in rows]
    valley_rows = [r for r in rows if VALLEY_E-5 <= r["timestamp_s"] <= VALLEY_E]
    post_rows   = [r for r in rows if 70 <= r["timestamp_s"] <= 88]
    print(f"[S2 GEN] {len(rows)} rows | Irr range {min(irrs):.0f}-{max(irrs):.0f} W/m2")
    print(f"         PV range {min(pvs):.0f}-{max(pvs):.0f}MW | Valley PV~{sum(r['pv_total_power_mw'] for r in valley_rows)/len(valley_rows):.0f}MW")
    print(f"         Post PV~{sum(r['pv_total_power_mw'] for r in post_rows)/len(post_rows):.0f}MW (surplus~{sum(r['pv_total_power_mw'] for r in post_rows)/len(post_rows)-PLAN_MW:.0f}MW)")
    return rows

def write_csv(rows, path):
    fields = ["timestamp_s","grid_frequency_hz","grid_voltage_pu","rocof_hz_per_s",
              "freq_deviation_hz","pv_total_power_mw","pv_inv1_power_mw",
              "pv_inv2_power_mw","pv_inv3_power_mw","plan_power_mw",
              "irradiance_w_per_m2","ess_soc_pct","ess_power_mw","comm_status"]
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        w.writerows(rows)
    print(f"[S2 GEN] → {path}")

if __name__ == "__main__":
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "..", "..", "test_cases", "s2_cloud", "s2_cloud_test.csv")
    rows = generate()
    write_csv(rows, os.path.normpath(out))
