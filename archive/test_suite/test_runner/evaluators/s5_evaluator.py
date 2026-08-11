#!/usr/bin/env python3
# s5_evaluator.py — S5 光储联合优化限电调度 评估器 (v2.0 工业级重构)
# ============================================================
# v2.0 变更 (2026-07-25):
#   基于 scene5_step() 控制代码完整重构。
#   旧版仅覆盖 SOC 边界保护 (2 项断言), v2.0 覆盖:
#     - 限电切入/退出时序 (事件驱动 Scene 断言)
#     - 抗震荡三件套: 死区 + ESS 变化率限幅 + PV 锁定
#     - ESS 充电方向、限电释放验证
#   所有阈值与 config.h + scene5_step() 严格对齐。
# ============================================================

import sys, os, math
from common_evaluator import (
    GREEN, RED, YELLOW, CYAN, RESET, BOLD,
    NOMINAL_FREQ_HZ,
    HYSTERESIS_SKIP_SECONDS,
    AssertionResult, BaseEvaluator,
    check_scene_event_driven,
    check_soc_safety,
    check_ess_power_limit,
)

# ═══════════════════════════════════════════════════════════════
# S5 评估器参数 — 与 config.h §9 + scene5_step() 严格对齐
# ═══════════════════════════════════════════════════════════════

# ── 限电触发/退出 (DecisionEngine main.cpp:256-264) ──
S5_RATIO_THRESHOLD       = 0.85     # ratio = plan/pv < 0.85 → 限电切入
S5_EXCESS_MIN_MW         = 2.0      # excess = pv - plan > 2MW → 触发确认

# ── 抗震荡三件套 (config.h §9) ──
S5_DEADBAND_MW           = 2.0      # 补丁#1: |surplus| < 2MW → ESS 冻结
S5_ESS_RATE_LIMIT_MW     = 2.0      # 补丁#2: |ΔTARGET| ≤ 2MW/cycle
S5_PV_LOCK_ERROR_PCT     = 0.05     # 补丁#3: 误差 < 5% → 锁定
S5_PV_LOCK_HOLD_CYCLES   = 3        # 补丁#3: 最小保持 3 周期

# ── 场景切换 ──
DEBOUNCE_CONFIRM_CYCLES  = 3        # 决策引擎防抖确认周期数

# ── 评估窗口 ──
PHASE_A_S, PHASE_A_E =  0.0, 30.0   # PV 爬坡: 验证 S5 切入
PHASE_B_S, PHASE_B_E = 35.0, 65.0   # 稳态限电: 验证抗震荡三件套
PHASE_C_S, PHASE_C_E = 70.0, 90.0   # 云层扰动: 验证死区防震荡
PHASE_D_S, PHASE_D_E = 95.0, 120.0  # PV 降载: 验证限电解除
PLAN_STEADY_MW         = 80.0        # 恒定计划功率


def _f(r, key): return float(r.get(key, 0.0))


# ═══════════════════════════════════════════════════════════════
# S5Evaluator — 光储联合优化限电调度评估器
# ═══════════════════════════════════════════════════════════════
class S5Evaluator(BaseEvaluator):
    """S5 光储联合优化限电调度场景评估器 (v2.0)

    评估维度:
      A 组 — 基础合规:  A1 PFR≡0 / A2 事件驱动 Scene≡5
      B 组 — 限电控制:  B1 ESS 充电方向 / B2 ESS 变化率限幅 /
                       B3 死区防震荡 / B4 PV 锁定稳定性
      C 组 — 退出验证:  C1 限电释放 (curtail_ratio→1.0)
      D 组 — 安全边界:  D1 SOC / D2 ESS 限幅
    """

    def __init__(self):
        super().__init__("S5", expected_scene=5)

    def default_result_csv(self) -> str:
        return "../../test_reports/s5_joint/s5_execution_result.csv"

    def evaluate(self, rows):
        ar = AssertionResult()

        # ─────────────────────────────────────────────────────
        # A1: PFR ≡ 0 — 限电场景非调频, 频率在死区内
        # ─────────────────────────────────────────────────────
        pfr_violations = []
        for r in rows:
            t = _f(r, "timestamp_s")
            pfr = _f(r, "p_pfr_mw")
            if t >= HYSTERESIS_SKIP_SECONDS and abs(pfr) > 0.1:
                pfr_violations.append((t, pfr))
        ar.check(len(pfr_violations) == 0,
                 f"S5-A1: PFR ≡ 0 全过程 (频率死区内) "
                 f"(违规 {len(pfr_violations)} 次)", "CRITICAL")
        if pfr_violations:
            for ts, val in pfr_violations[:3]:
                ar.details.append((False, "CRITICAL",
                                   f"  ↳ t={ts:.1f}s: PFR={val:.2f}MW"))

        # ─────────────────────────────────────────────────────
        # A2: 事件驱动 Scene=5 — 基于 ratio<0.85 触发
        #     使用 check_scene_event_driven 的变体:
        #     检测 ratio 首次 < 0.85 的时间点作为事件触发
        # ─────────────────────────────────────────────────────
        t_curtail_start = None
        t_curtail_end = None
        prev_curtailed = False
        for r in rows:
            t = _f(r, "timestamp_s")
            pv = _f(r, "p_pv_total_mw")
            plan = _f(r, "p_plan_mw")
            ratio = plan / pv if pv > 1.0 else 1.0
            excess = pv - plan
            curtailed = (ratio < S5_RATIO_THRESHOLD and excess > S5_EXCESS_MIN_MW)

            if not prev_curtailed and curtailed:
                if t_curtail_start is None:
                    t_curtail_start = t
            if prev_curtailed and not curtailed:
                if t_curtail_end is None and t_curtail_start is not None:
                    t_curtail_end = t
            prev_curtailed = curtailed

        # 统计 Scene=5 违规
        scene5_violations = []
        scene5_stable = {"in_s5": 0, "total": 0}
        t_assert_5 = (t_curtail_start or 0) + DEBOUNCE_CONFIRM_CYCLES

        for r in rows:
            t = _f(r, "timestamp_s")
            sc = _f(r, "active_scene")
            pv = _f(r, "p_pv_total_mw")
            plan = _f(r, "p_plan_mw")
            ratio = plan / pv if pv > 1.0 else 1.0
            excess = pv - plan
            in_curtail = (ratio < S5_RATIO_THRESHOLD and excess > S5_EXCESS_MIN_MW)

            if t_curtail_start and t >= t_assert_5 and in_curtail:
                scene5_stable["total"] += 1
                if abs(sc - 5.0) <= 0.5:
                    scene5_stable["in_s5"] += 1
                else:
                    scene5_violations.append((t, sc, "限电窗口内Scene≠5"))

        ar.check(len(scene5_violations) == 0,
                 f"S5-A2: 事件驱动 Scene ≡ 5 "
                 f"(ratio<{S5_RATIO_THRESHOLD}, 防抖 {DEBOUNCE_CONFIRM_CYCLES} 周期)  "
                 f"(违规 {len(scene5_violations)} 次)", "CRITICAL")
        if scene5_violations:
            for ts, val, desc in scene5_violations[:5]:
                ar.details.append((False, "CRITICAL",
                                   f"  ↳ t={ts:.1f}s: Scene={val:.0f} ({desc})"))

        # Scene 稳定性
        if scene5_stable["total"] > 0:
            pct = 100.0 * scene5_stable["in_s5"] / scene5_stable["total"]
            ar.check(pct >= 90.0,
                     f"S5-A2b: 限电期 Scene=5 占比 "
                     f"{scene5_stable['in_s5']}/{scene5_stable['total']} "
                     f"({pct:.1f}%) ≥ 90%", "CRITICAL")

        # 必须检测到 ratio 穿越
        pv_at_cross = 0.0
        if t_curtail_start is not None:
            cross_rows = [r for r in rows
                         if abs(_f(r, 'timestamp_s') - t_curtail_start) < 0.5]
            if cross_rows:
                pv_at_cross = _f(cross_rows[0], 'pv_total_power_mw')

        ar.check(t_curtail_start is not None,
                 f"S5-A2: 检测到 ratio<{S5_RATIO_THRESHOLD} 事件 "
                 f"(首次 PV={pv_at_cross:.0f}MW @ "
                 f"t={t_curtail_start:.1f}s)" if t_curtail_start
                 else f"S5-A2: 未检测到 ratio<{S5_RATIO_THRESHOLD} — 激励剖面不足",
                 "CRITICAL")

        # ─────────────────────────────────────────────────────
        # B1: ESS 充电方向 — 盈余期 ESS 必须充电 (负功率)
        #     scene5_step(): surplus>0 && soc<80% → 充电
        # ─────────────────────────────────────────────────────
        phase_b_rows = [r for r in rows
                        if PHASE_B_S <= _f(r, "timestamp_s") <= PHASE_B_E]
        if phase_b_rows:
            ess_b = [_f(r, "p_ess_target_mw") for r in phase_b_rows]
            ess_b_mean = sum(ess_b) / len(ess_b)
            # ESS target in CSV = ESS_01.TARGET_POWER = -ess_target
            # S5 writes negative for charge (ess_target>0 → write -ess_target < 0)
            # So p_ess_target_mw < 0 means ESS is charging (absorbing surplus)
            ar.check(ess_b_mean < 0.0,
                     f"S5-B1: 盈余期 ESS 充电方向 "
                     f"P_ESS_mean={ess_b_mean:.2f}MW < 0 (充电吸收盈余) "
                     f"(Phase B t∈[{PHASE_B_S:.0f},{PHASE_B_E:.0f}]s)",
                     "CRITICAL")
        else:
            ar.check(False, "S5-B1: Phase B 无数据", "CRITICAL")

        # ─────────────────────────────────────────────────────
        # B2: ESS 变化率限幅 — |ΔTARGET| ≤ 2MW/cycle
        #     scene5_step() 补丁#2: ess_delta clamped to ±2MW
        # ─────────────────────────────────────────────────────
        # ★ v4.35: 仅检查 Scene=5 期间的 ESS 变化率 (过渡边界豁免)
        ess_targets = [(_f(r, "timestamp_s"), _f(r, "p_ess_target_mw"))
                       for r in rows
                       if _f(r, "timestamp_s") >= HYSTERESIS_SKIP_SECONDS
                       and abs(_f(r, "active_scene") - 5.0) <= 0.5]
        rate_violations = []
        for i in range(1, len(ess_targets)):
            t_prev, ess_prev = ess_targets[i-1]
            t_curr, ess_curr = ess_targets[i]
            delta = abs(ess_curr - ess_prev)
            if delta > S5_ESS_RATE_LIMIT_MW + 0.05:
                rate_violations.append((t_curr, delta, ess_prev, ess_curr))

        ar.check(len(rate_violations) == 0,
                 f"S5-B2: ESS 变化率限幅 |ΔTARGET| ≤ {S5_ESS_RATE_LIMIT_MW}MW/cycle "
                 f"(补丁#2)  (违规 {len(rate_violations)} 次)", "CRITICAL")
        if rate_violations:
            for ts, delta, prev, curr in rate_violations[:5]:
                ar.details.append((False, "CRITICAL",
                                   f"  ↳ t={ts:.1f}s: |Δ|={delta:.2f}MW "
                                   f"({prev:.2f}→{curr:.2f})"))

        # ─────────────────────────────────────────────────────
        # B3: 死区防震荡 — Phase C (云层扰动) ESS 不震荡
        #     scene5_step() 补丁#1: |surplus|<2MW → ESS 维持
        #     验证: Phase C 中 ESS 目标变化次数少 / 无符号翻转
        # ─────────────────────────────────────────────────────
        phase_c_rows = [r for r in rows
                        if PHASE_C_S <= _f(r, "timestamp_s") <= PHASE_C_E]
        if phase_c_rows:
            ess_c = [_f(r, "p_ess_target_mw") for r in phase_c_rows]
            # 统计符号翻转次数 (充电↔放电)
            sign_flips = 0
            prev_sign = 1 if ess_c[0] >= 0 else -1
            for v in ess_c[1:]:
                cur_sign = 1 if v >= 0 else -1
                if cur_sign != prev_sign and abs(v) > 0.1:
                    sign_flips += 1
                if abs(v) > 0.1:
                    prev_sign = cur_sign

            ar.check(sign_flips <= 1,
                     f"S5-B3: Phase C 死区防震荡 "
                     f"ESS 符号翻转 {sign_flips} 次 ≤ 1 "
                     f"(补丁#1: |surplus|<{S5_DEADBAND_MW}MW→冻结) "
                     f"(t∈[{PHASE_C_S:.0f},{PHASE_C_E:.0f}]s)",
                     "CRITICAL")
        else:
            ar.check(False, "S5-B3: Phase C 无数据", "WARN")

        # ─────────────────────────────────────────────────────
        # B4: PV 锁定稳定性 — 稳态期 PV 目标变化次数少
        #     scene5_step() 补丁#3: 误差<5% → 锁定 curtail_ratio
        #     验证: Phase B 稳态段 PV 目标功率方差小
        # ─────────────────────────────────────────────────────
        pv_targets_b = [(_f(r, "timestamp_s"), _f(r, "p_pv_target_total_mw"))
                        for r in rows
                        if PHASE_B_S <= _f(r, "timestamp_s") <= PHASE_B_E]
        if pv_targets_b:
            pv_t_vals = [v for _, v in pv_targets_b]
            pv_t_mean = sum(pv_t_vals) / len(pv_t_vals)
            # 计算 PV 目标的变化率: 相邻周期变化 > 1MW 的次数
            pv_changes = 0
            for i in range(1, len(pv_t_vals)):
                if abs(pv_t_vals[i] - pv_t_vals[i-1]) > 1.0:
                    pv_changes += 1

            ar.check(pv_changes <= 3,
                     f"S5-B4: PV 锁定稳定性 "
                     f"大幅变化 {pv_changes} 次 ≤ 3 "
                     f"(补丁#3: curtail_ratio 锁定 ≥{S5_PV_LOCK_HOLD_CYCLES} 周期) "
                     f"(t∈[{PHASE_B_S:.0f},{PHASE_B_E:.0f}]s)",
                     "CRITICAL")
        else:
            ar.check(False, "S5-B4: Phase B 无数据", "WARN")

        # ─────────────────────────────────────────────────────
        # C1: 限电解除 — Phase D 末 S5 应成功自愈退出
        #     Phase D 横跨限电激活期和自愈退出期:
        #     - 前期: PV > 94MW, ratio<0.85, S5 限电中
        #     - 后期: PV 降至 94MW 以下, ratio≥0.85 → all_healthy
        #             → 连续 3 周期 → S5→S1 (自愈退出)
        #     验证: Phase D 最后几个周期 Scene 应回归 1
        #     注: 退出后 Scene 1 写 PV target≈80MW (plan), 而非 0
        # ─────────────────────────────────────────────────────
        phase_d_scenes = [_f(r, "active_scene")
                          for r in rows
                          if PHASE_D_S <= _f(r, "timestamp_s") <= PHASE_D_E]
        if phase_d_scenes:
            # 取最后 20% 时段的场景号
            n = len(phase_d_scenes)
            late_scenes = phase_d_scenes[-(max(n//5, 3)):]  # 至少 3 个周期

            # 判定: 最后几周期应有 Scene=1 (自愈退出) 或 Scene 正在从 5 切换
            late_has_s1 = any(abs(s - 1.0) <= 0.5 for s in late_scenes)
            late_scene_mean = sum(late_scenes) / len(late_scenes)

            # 工业标准: Phase D 结束时 S5 应已退出或正在退出
            released = late_has_s1 or late_scene_mean < 3.0

            ar.check(released,
                     f"S5-C1: 限电解除 Phase D 末期 Scene "
                     f"(最后{len(late_scenes)}周期) = "
                     f"{', '.join(f'{s:.0f}' for s in late_scenes)} "
                     f"→ {'✅ 已退出至S1' if late_has_s1 else '⚠ S5仍激活' if late_scene_mean > 4 else '🔄 退出中'} "
                     f"(t∈[{PHASE_D_S:.0f},{PHASE_D_E:.0f}]s)",
                     "CRITICAL")
        else:
            ar.check(False, "S5-C1: Phase D 无数据", "WARN")

        # ─────────────────────────────────────────────────────
        # D1/D2: 安全边界
        # ─────────────────────────────────────────────────────
        check_soc_safety(ar, rows, "S5-D1")
        check_ess_power_limit(ar, rows, "S5-D2")

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

        ts    = [float(r["timestamp_s"]) for r in rows]
        pv    = [float(r["p_pv_total_mw"]) for r in rows]
        plan  = [float(r["p_plan_mw"]) for r in rows]
        ess   = [float(r["p_ess_target_mw"]) for r in rows]
        pcc   = [float(r["p_pcc_total_mw"]) for r in rows]
        soc   = [float(r["ess_soc_pct"]) for r in rows]
        scene = [float(r["active_scene"]) for r in rows]
        pv_tgt = [float(r["p_pv_target_total_mw"]) for r in rows]

        # 计算 ratio 曲线
        ratio = [plan[i] / pv[i] if pv[i] > 1 else 1.0 for i in range(len(ts))]

        fig, axes = plt.subplots(4, 1, figsize=(16, 14), sharex=True)

        # ── 子图 1: PV + Plan + Ratio + 限电阈值 ──
        ax1 = axes[0]
        ax1.plot(ts, pv, "#DAA520", lw=1.5, label="PV Available")
        ax1.plot(ts, plan, "k--", lw=1.5, alpha=0.7, label="SCADA Plan (80MW)")
        ax1.axhline(80 / S5_RATIO_THRESHOLD, color="red", ls=":", alpha=0.6, lw=1.2,
                    label=f"PV limit (ratio={S5_RATIO_THRESHOLD}, {80/S5_RATIO_THRESHOLD:.0f}MW)")
        ax1_ratio = ax1.twinx()
        ax1_ratio.plot(ts, ratio, "#1E90B2", lw=1, alpha=0.6, label="ratio=plan/PV")
        ax1_ratio.axhline(S5_RATIO_THRESHOLD, color="red", ls="-.", alpha=0.4, lw=0.8)
        ax1_ratio.set_ylabel("ratio (plan/PV)", color="#1E90B2")
        ax1_ratio.set_ylim(0.5, 1.1)
        ax1.set_ylabel("Power (MW)")
        ax1.set_title("S5: PV+ESS Joint Curtailment — PV vs Plan & Curtailment Ratio")
        lines1, labels1 = ax1.get_legend_handles_labels()
        lines2, labels2 = ax1_ratio.get_legend_handles_labels()
        ax1.legend(lines1 + lines2, labels1 + labels2, fontsize=7, loc="upper left")
        ax1.grid(alpha=0.3)

        # ── 子图 2: ESS 目标功率 + 变化率限幅 ──
        axes[1].fill_between(ts, -S5_ESS_RATE_LIMIT_MW, S5_ESS_RATE_LIMIT_MW,
                             alpha=0.06, color="orange",
                             label=f"Rate limit band ±{S5_ESS_RATE_LIMIT_MW}MW/cycle")
        axes[1].plot(ts, ess, "#228B22", lw=1.5, label="ESS Target (per unit)")
        axes[1].axhline(0, color="gray", ls="--", alpha=0.4)
        # 标注 Δ 超限点
        ess_deltas = []
        for i in range(1, len(ts)):
            d = abs(ess[i] - ess[i-1])
            if d > S5_ESS_RATE_LIMIT_MW + 0.05:
                ess_deltas.append((ts[i], d))
        if ess_deltas:
            td, dd = zip(*ess_deltas)
            axes[1].scatter(td, [ess[ts.index(t)] for t in td],
                           color="red", s=20, zorder=5, marker='x',
                           label=f"Rate violation ({len(ess_deltas)} pts)")
        axes[1].set_ylabel("ESS (MW)", color="#228B22")
        axes[1].set_ylim(-12, 12)  # ★ v4.35: 固定范围, 微幅充放电可见
        axes[1].tick_params(axis="y", labelcolor="#228B22")
        # SOC 叠加 (双Y轴)
        ax1b = axes[1].twinx()
        ax1b.plot(ts, soc, "#1E90B2", lw=1.2, alpha=0.7, label="ESS SOC")
        ax1b.set_ylabel("SOC (%)", color="#1E90B2")
        ax1b.set_ylim(60, 62)  # ★ v4.35: 20MWh电池7MW充电, 120s仅充0.4%, 缩轴可见
        ax1b.tick_params(axis="y", labelcolor="#1E90B2")
        axes[1].set_title("ESS Target Power & SOC — Rate Limiter (Patch #2)")
        l1, lb1 = axes[1].get_legend_handles_labels()
        l2, lb2 = ax1b.get_legend_handles_labels()
        axes[1].legend(l1+l2, lb1+lb2, fontsize=7, loc="upper right")
        axes[1].grid(alpha=0.3)

        # ── 子图 3: 场景号 + PV 锁定状态 ──
        axes[2].step(ts, scene, "#6A0DAD", lw=1.8, where="post", label="Active Scene")
        axes[2].axhline(5, color="#DC143C", ls=":", alpha=0.5, lw=1, label="Scene 5 (curtail)")
        axes[2].axhline(1, color="#228B22", ls=":", alpha=0.5, lw=1, label="Scene 1 (steady)")
        # PV 目标 vs PV 可用 (反映 curtailment)
        axes[2].fill_between(ts, pv_tgt, pv, alpha=0.15, color="orange",
                             label="Curtailed PV (Δ from available)")
        axes[2].plot(ts, pv_tgt, "#DAA520", lw=0.8, alpha=0.6, label="PV Target (curtailed)")
        axes[2].set_ylabel("Scene / Power (MW)")
        axes[2].set_title("Scene Switching + PV Curtailment (Patch #3 lock)")
        axes[2].set_ylim(0, 7)
        axes[2].legend(fontsize=7, loc="upper right")
        axes[2].grid(alpha=0.3)

        # ── 子图 4: PCC vs Plan + SOC ──
        ax4 = axes[3]
        ax4.plot(ts, pcc, "#DC143C", lw=1.2, label="PCC Total")
        ax4.plot(ts, plan, "k--", lw=1.5, alpha=0.7, label="Plan (80MW)")
        ax4_soc = ax4.twinx()
        ax4_soc.plot(ts, soc, "#1E90B2", lw=1.2, alpha=0.7, label="SOC")
        ax4_soc.set_ylabel("SOC (%)", color="#1E90B2")
        ax4_soc.set_ylim(0, 100)
        ax4.set_ylabel("Power (MW)")
        ax4.set_xlabel("Time (s)")
        ax4.set_title("PCC vs Plan & ESS SOC")
        lines1, labels1 = ax4.get_legend_handles_labels()
        lines2, labels2 = ax4_soc.get_legend_handles_labels()
        ax4.legend(lines1 + lines2, labels1 + labels2, fontsize=7, loc="upper right")
        ax4.grid(alpha=0.3)

        # 标注 Phase 区域
        for ax in axes:
            ax.axvspan(PHASE_A_S, PHASE_A_E, alpha=0.04, color="blue", label="_A: Ramp")
            ax.axvspan(PHASE_B_S, PHASE_B_E, alpha=0.04, color="green", label="_B: Steady")
            ax.axvspan(PHASE_C_S, PHASE_C_E, alpha=0.04, color="orange", label="_C: Cloud")
            ax.axvspan(PHASE_D_S, PHASE_D_E, alpha=0.04, color="red", label="_D: Release")

        plt.tight_layout()
        os.makedirs(os.path.dirname(png) or ".", exist_ok=True)
        plt.savefig(png, dpi=150, bbox_inches="tight")
        print(f"{GREEN}[PLOT] {png}{RESET}")
        plt.close()


# ═══════════════════════════════════════════════════════════════
# 入口
# ═══════════════════════════════════════════════════════════════
if __name__ == "__main__":
    sys.exit(0 if S5Evaluator().run(sys.argv) else 1)
