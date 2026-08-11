#!/usr/bin/env python3
# s4_evaluator.py — S4 一次调频 PFR 评估器 (v2.0 工业级重构)
# ============================================================
# v2.0 变更 (2026-07-25):
#   - S4-A1:  死区断言改为检查 ESS 实际调度功率 (p_ess_target_mw),
#             替代 Runner 理论 PFR 值, 增加 0.5MW 物理容忍度。
#   - S4-A1b: 新增死区退场容忍窗口断言, 验证 ESS 平滑退场行为。
#   - S4-A2:  从绝对时间戳断言改为事件驱动场景切换断言,
#             对齐 DecisionEngine 的防抖迟滞机制。
#   - 所有时间窗口参数集中定义为可配置常量。
# ============================================================

import sys, os, math
from common_evaluator import (
    GREEN, RED, YELLOW, CYAN, RESET, BOLD,
    NOMINAL_FREQ_HZ, PFR_DEADBAND_HZ,
    HYSTERESIS_SKIP_SECONDS,
    AssertionResult, BaseEvaluator,
    check_scene_event_driven,
    check_deadband_ramp_down,
    check_soc_safety,
    check_ess_power_limit,
)

# ═══════════════════════════════════════════════════════════════
# S4 评估器可配置参数 — 所有阈值集中定义, 便于调优与审查
# ═══════════════════════════════════════════════════════════════

# ── 死区容忍 ──
ESS_DEADBAND_TOLERANCE_MW  = 0.5       # 死区内 ESS 目标功率容忍上限 (MW)
                                       # 考虑 ESS 一阶惯性 τ=200ms  + 量测噪声
PFR_THEORY_WARN_THRESHOLD  = 1.0       # 理论 PFR 警告阈值 (MW, 仅 WARN 不阻断)
RAMP_DOWN_GRACE_CYCLES     = 5         # 退场过渡容忍控制周期数 (=0.5s @ 100ms)
RAMP_DOWN_GRACE_SEC        = 0.5       # 退场过渡容忍时间 (s)

# ── 场景切换 ──
DEBOUNCE_CONFIRM_CYCLES    = 3         # 决策引擎防抖确认所需控制周期数
                                       # 对齐 stable_counter≥2 + 1 周期裕度
FREQ_DEADBAND_HZ           = 0.05      # 频率死区阈值 (Hz)

# ── 评估时间窗口 (基于测试激励频率剖面) ──
# 这些窗口定义频偏稳态评估段, 用于 Droop 精度和方向性判定
# 窗口基于频率事件而非绝对时间 — 若激励剖面变更需同步调整
DB_INITIAL_S   = 3.0       # 初始死区段起始 (s), 跳过 HYSTERESIS_SKIP_SECONDS
DB_INITIAL_E   = 15.0      # 初始死区段结束 (s), 频率斜坡开始前
LOW_FREQ_S     = 30.0      # 低频稳态段起始 (s)
LOW_FREQ_E     = 45.0      # 低频稳态段结束 (s)
HI_FREQ_S      = 70.0      # 高频稳态段起始 (s)
HI_FREQ_E      = 80.0      # 高频稳态段结束 (s)

# ── 一次调频 Droop 特性参数 ──
DROOP_K = -60.0            # Droop 系数: ΔP = -60 × (f - 50.0) MW
                           # 等效于 R=4%, Pn=120MW, fn=50Hz

# ── Droop 精度判定 ──
DROOP_ACCURACY_MAX_ERR_PCT = 15.0  # Droop 稳态精度最大允许误差 (%)
PFR_RESPONSE_MAX_LATENCY_S = 0.5   # PFR 响应最大允许延迟 (s)


# ═══════════════════════════════════════════════════════════════
# S4Evaluator — 一次调频全工况评估器
# ═══════════════════════════════════════════════════════════════
class S4Evaluator(BaseEvaluator):
    """S4 PFR 一次调频场景评估器 (v2.0)

    评估维度:
      A 组 — 死区合规:  A1 初始死区 / A1b 退场容忍
      B 组 — 调频性能:  B1 Droop 精度 / B2 响应延迟
      C 组 — 方向正确:  C1 高频段减发
      D 组 — 安全边界:  D1 SOC / D2 ESS 限幅
    """

    def __init__(self):
        super().__init__("S4", expected_scene=4)

    def default_result_csv(self) -> str:
        return "../../test_reports/s4_pfr/s4_execution_result.csv"

    def evaluate(self, rows):
        ar = AssertionResult()
        f = lambda r, key: float(r.get(key, 0.0))

        # ─────────────────────────────────────────────────────
        # A1: 初始死区 ESS 调度断言
        #     验证主控制器在死区内未错误激活 PFR 调度
        #     ★ v2.0: 检查 p_ess_target_mw (主控制器实际输出)
        #       而非 p_pfr_mw (Runner 理论计算值)
        # ─────────────────────────────────────────────────────
        db_ess = [abs(f(r, "p_ess_target_mw")) for r in rows
                  if DB_INITIAL_S <= f(r, "timestamp_s") <= DB_INITIAL_E
                  and f(r, "timestamp_s") >= HYSTERESIS_SKIP_SECONDS]

        if db_ess:
            ess_max = max(db_ess)
            ar.check(ess_max < ESS_DEADBAND_TOLERANCE_MW,
                     f"S4-A1: 初始死区 |P_ESS_target|max={ess_max:.4f}MW "
                     f"< {ESS_DEADBAND_TOLERANCE_MW}MW "
                     f"(t∈[{DB_INITIAL_S:.0f},{DB_INITIAL_E:.0f}]s)",
                     "CRITICAL")
        else:
            ar.check(False,
                     f"S4-A1: 初始死区无有效数据 (t∈[{DB_INITIAL_S:.0f},{DB_INITIAL_E:.0f}]s)",
                     "CRITICAL")

        # ── A1 子检查: 理论 PFR 噪音监控 (仅 WARN, 不阻断) ──
        db_pfr_theory = [abs(f(r, "p_pfr_mw")) for r in rows
                         if DB_INITIAL_S <= f(r, "timestamp_s") <= DB_INITIAL_E
                         and f(r, "timestamp_s") >= HYSTERESIS_SKIP_SECONDS]
        if db_pfr_theory:
            pfr_max = max(db_pfr_theory)
            ar.check(pfr_max < PFR_THEORY_WARN_THRESHOLD,
                     f"S4-A1(i): 死区理论PFR噪声 "
                     f"|PFR_theory|max={pfr_max:.4f}MW "
                     f"< {PFR_THEORY_WARN_THRESHOLD}MW (信息性)",
                     "WARN")

        # ─────────────────────────────────────────────────────
        # A1b: 死区退场容忍窗口断言
        #      频率从频偏回归死区时, ESS 功率需平滑退场
        #      允许 RAMP_DOWN_GRACE_CYCLES 个控制周期的过渡
        # ─────────────────────────────────────────────────────
        check_deadband_ramp_down(
            ar, rows,
            freq_field="grid_frequency_hz",
            power_field="p_ess_target_mw",
            deadband_hz=FREQ_DEADBAND_HZ,
            grace_cycles=RAMP_DOWN_GRACE_CYCLES,
            tolerance_mw=ESS_DEADBAND_TOLERANCE_MW,
            assertion_id="S4-A1b"
        )

        # ─────────────────────────────────────────────────────
        # A2: 事件驱动场景切换断言
        #     ★ v2.0: 替代 check_scene_identity 的绝对时间戳逻辑
        #     基于 Δf 突破死区事件触发判定, 对齐决策引擎防抖
        # ─────────────────────────────────────────────────────
        t_first, t_ret1, t_second, t_ret2 = check_scene_event_driven(
            ar, rows,
            freq_field="grid_frequency_hz",
            scene_field="active_scene",
            deadband_hz=FREQ_DEADBAND_HZ,
            debounce_cycles=DEBOUNCE_CONFIRM_CYCLES,
            expected_scene=4,
            exit_scene=1,
            assertion_id="S4-A2"
        )

        # ─────────────────────────────────────────────────────
        # B1: Droop 精度 — 低频稳态段 (30~45s)
        #     验证实际 PFR 功率与 Droop 理论值的偏差
        # ─────────────────────────────────────────────────────
        low_rows = [r for r in rows
                    if LOW_FREQ_S <= f(r, "timestamp_s") <= LOW_FREQ_E]
        if low_rows:
            pfr_act = [f(r, "p_pfr_mw") for r in low_rows]
            freq_vals = [f(r, "grid_frequency_hz") for r in low_rows]
            freq_avg = sum(freq_vals) / len(freq_vals)
            pfr_th = DROOP_K * (freq_avg - NOMINAL_FREQ_HZ)
            pfr_av = sum(pfr_act) / len(pfr_act)

            if abs(pfr_th) > 0.1:
                err_pct = abs(pfr_av - pfr_th) / abs(pfr_th) * 100.0
                ar.check(err_pct < DROOP_ACCURACY_MAX_ERR_PCT,
                         f"S4-B1: Droop精度 err={err_pct:.1f}% "
                         f"< {DROOP_ACCURACY_MAX_ERR_PCT:.0f}% "
                         f"(f_avg={freq_avg:.4f}Hz, "
                         f"PFR_act={pfr_av:.2f}MW, "
                         f"PFR_th={pfr_th:.2f}MW)",
                         "CRITICAL")
            else:
                ar.check(False,
                         f"S4-B1: Droop理论值≈0 (f_avg={freq_avg:.4f}Hz) "
                         f"— 低频段频率偏差不足, 检查激励数据",
                         "CRITICAL")
        else:
            ar.check(False,
                     f"S4-B1: 低频稳态段无数据 "
                     f"(t∈[{LOW_FREQ_S:.0f},{LOW_FREQ_E:.0f}]s)",
                     "CRITICAL")

        # ─────────────────────────────────────────────────────
        # B2: PFR 响应延迟
        #     从频率首次突破死区到 PFR 功率 > 0.1MW 的延迟
        # ─────────────────────────────────────────────────────
        t_freq_cross = next(
            (f(r, "timestamp_s") for r in rows
             if abs(f(r, "grid_frequency_hz") - NOMINAL_FREQ_HZ) > FREQ_DEADBAND_HZ),
            None
        )
        t_pfr_active = next(
            (f(r, "timestamp_s") for r in rows
             if f(r, "timestamp_s") >= (t_freq_cross or 0.0)
             and abs(f(r, "p_pfr_mw")) > 0.1),
            None
        )

        if t_freq_cross is not None and t_pfr_active is not None:
            latency = t_pfr_active - t_freq_cross
            ar.check(latency <= PFR_RESPONSE_MAX_LATENCY_S,
                     f"S4-B2: PFR响应延迟={latency:.2f}s "
                     f"≤ {PFR_RESPONSE_MAX_LATENCY_S}s "
                     f"(f_cross@{t_freq_cross:.1f}s, "
                     f"PFR_active@{t_pfr_active:.1f}s)",
                     "CRITICAL")
        else:
            detail = ""
            if t_freq_cross is None:
                detail = " (未检测到频率越死区)"
            elif t_pfr_active is None:
                detail = " (频率越死区但未检测到PFR功率响应)"
            ar.check(False,
                     f"S4-B2: PFR响应检测失败{detail}",
                     "CRITICAL")

        # ─────────────────────────────────────────────────────
        # C1: 高频段方向正确性 (70~80s)
        #     高频时应减发/充电 → PFR 均值 < 0
        # ─────────────────────────────────────────────────────
        hi_pfr = [f(r, "p_pfr_mw") for r in rows
                  if HI_FREQ_S <= f(r, "timestamp_s") <= HI_FREQ_E]
        if hi_pfr:
            hi_mean = sum(hi_pfr) / len(hi_pfr)
            ar.check(hi_mean < 0.0,
                     f"S4-C1: 高频段 PFR均值={hi_mean:.1f}MW < 0 "
                     f"(减发/充电) "
                     f"(t∈[{HI_FREQ_S:.0f},{HI_FREQ_E:.0f}]s)",
                     "CRITICAL")
        else:
            ar.check(False,
                     f"S4-C1: 高频段无数据 "
                     f"(t∈[{HI_FREQ_S:.0f},{HI_FREQ_E:.0f}]s)",
                     "CRITICAL")

        # ─────────────────────────────────────────────────────
        # D1/D2: 安全边界 — SOC + ESS 功率限幅
        # ─────────────────────────────────────────────────────
        check_soc_safety(ar, rows, "S4-D1")
        check_ess_power_limit(ar, rows, "S4-D2")

        return ar

    # ═══════════════════════════════════════════════════════════
    # 可视化分析
    # ═══════════════════════════════════════════════════════════
    def plot_analysis(self, rows, png):
        try:
            import matplotlib
            matplotlib.use("Agg")
            import matplotlib.pyplot as plt
        except ImportError:
            print(f"{YELLOW}[SKIP] matplotlib missing{RESET}")
            return

        ts   = [float(r["timestamp_s"]) for r in rows]
        freq = [float(r["grid_frequency_hz"]) for r in rows]
        pfr  = [float(r["p_pfr_mw"]) for r in rows]
        ess  = [float(r["p_ess_target_mw"]) for r in rows]
        pcc  = [float(r["p_pcc_total_mw"]) for r in rows]
        plan = [float(r["p_plan_mw"]) for r in rows]
        scene = [float(r["active_scene"]) for r in rows]

        fig, axes = plt.subplots(4, 1, figsize=(16, 14), sharex=True)

        # ── 子图 1: 频率 + 死区边界 ──
        axes[0].plot(ts, freq, "b-", lw=1.5, label="Grid Frequency")
        axes[0].axhline(NOMINAL_FREQ_HZ + FREQ_DEADBAND_HZ,
                        color="red", ls=":", alpha=0.6, lw=1.2,
                        label=f"Deadband ±{FREQ_DEADBAND_HZ}Hz")
        axes[0].axhline(NOMINAL_FREQ_HZ - FREQ_DEADBAND_HZ,
                        color="red", ls=":", alpha=0.6, lw=1.2)
        axes[0].axhline(NOMINAL_FREQ_HZ, color="gray", ls="--", alpha=0.4)
        axes[0].set_ylabel("Frequency (Hz)")
        axes[0].set_title("S4 PFR: Grid Frequency & Deadband")
        axes[0].legend(fontsize=7, loc="upper right")
        axes[0].grid(alpha=0.3)

        # ── 子图 2: PFR 功率 (实际 vs 理论) + ESS 调度 ──
        axes[1].plot(ts, pfr, "#DC143C", lw=1.5, label="PFR (actual)")
        pfr_th_line = [DROOP_K * (f - NOMINAL_FREQ_HZ)
                       if abs(f - NOMINAL_FREQ_HZ) > FREQ_DEADBAND_HZ
                       else 0.0
                       for f in freq]
        axes[1].plot(ts, pfr_th_line, "orange", ls="--", lw=1, alpha=0.7,
                     label="PFR (Droop theory)")
        axes[1].plot(ts, ess, "#228B22", lw=1.2, alpha=0.8,
                     label="ESS Target (AGC dispatch)")
        axes[1].axhline(0, color="gray", ls="--", alpha=0.4)
        axes[1].axhspan(-ESS_DEADBAND_TOLERANCE_MW, ESS_DEADBAND_TOLERANCE_MW,
                        alpha=0.08, color="green",
                        label=f"ESS deadband ±{ESS_DEADBAND_TOLERANCE_MW}MW")
        axes[1].set_ylabel("Power (MW)")
        axes[1].set_title("PFR Power & ESS Dispatch")
        axes[1].legend(fontsize=7, loc="upper right")
        axes[1].grid(alpha=0.3)

        # ── 子图 3: 场景号 ──
        axes[2].step(ts, scene, "#6A0DAD", lw=1.8, where="post",
                     label="Active Scene")
        axes[2].axhline(4, color="#DC143C", ls=":", alpha=0.5, lw=1)
        axes[2].axhline(1, color="#228B22", ls=":", alpha=0.5, lw=1)
        axes[2].set_ylabel("Scene #")
        axes[2].set_title("Scene Switching (event-driven tracking)")
        axes[2].set_ylim(0, 6)
        axes[2].set_yticks([0, 1, 2, 3, 4, 5, 6])
        axes[2].legend(fontsize=7, loc="upper right")
        axes[2].grid(alpha=0.3)

        # ── 子图 4: PCC vs Plan ──
        axes[3].plot(ts, pcc, "#DC143C", lw=1.2, label="PCC Total")
        axes[3].plot(ts, plan, "k--", lw=1.5, alpha=0.7, label="Plan")
        axes[3].set_ylabel("Power (MW)")
        axes[3].set_xlabel("Time (s)")
        axes[3].set_title("PCC vs Plan (tracking)")
        axes[3].legend(fontsize=7, loc="upper right")
        axes[3].grid(alpha=0.3)

        plt.tight_layout()
        os.makedirs(os.path.dirname(png) or ".", exist_ok=True)
        plt.savefig(png, dpi=150, bbox_inches="tight")
        print(f"{GREEN}[PLOT] {png}{RESET}")
        plt.close()


# ═══════════════════════════════════════════════════════════════
# 入口
# ═══════════════════════════════════════════════════════════════
if __name__ == "__main__":
    sys.exit(0 if S4Evaluator().run(sys.argv) else 1)
