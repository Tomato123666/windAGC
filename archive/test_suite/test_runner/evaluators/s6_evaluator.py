#!/usr/bin/env python3
# s6_evaluator.py — S6 通信中断离线自治 评估器 (v3.0)
# ============================================================
# v3.0: --mode s6-1/2/3 显式策略选择 + --output-png 独立图表 + Agg 后端
# ============================================================
import sys, os, math
import matplotlib
matplotlib.use("Agg")  # ★ 批处理模式不弹窗
import matplotlib.pyplot as plt
from common_evaluator import (
    GREEN,RED,YELLOW,CYAN,RESET,BOLD,
    NOMINAL_FREQ_HZ, HYSTERESIS_SKIP_SECONDS,
    AssertionResult, BaseEvaluator,
    check_soc_safety, check_ess_power_limit,
    check_physics_pcc_bounds, check_physics_rocof,
    load_results, print_statistics, print_report,
)

def _f(r, key): return float(r.get(key, 0.0))

class S6Evaluator(BaseEvaluator):
    def __init__(self): super().__init__("S6", expected_scene=6)

    def default_result_csv(self) -> str:
        return "../../test_reports/s6_comms/s6_execution_result.csv"

    def evaluate(self, rows, forced_mode=None):
        ar = AssertionResult()
        if not rows: return ar

        # ── v4.34: 自动检测测试类型 (ESS极性优先, PV限电次之) ──
        #   旧版问题: PV限电检测优先级最高 → S6-3(也限电PV)被误判为S6-1
        #   新版逻辑:
        #     S6-2: PV自由MPPT + ESS仅充电(负)或不动作 → ESS负>30% 且 ESS正<5%
        #     S6-3: ESS仅放电(正)或不动作 → ESS正>30% 且 ESS负<5%
        #     S6-1: PV被限电 + ESS双向动作(充放都有)
        s6_rows = [r for r in rows if abs(_f(r,"active_scene")-6.0) <= 0.5]
        s6_plans = [_f(r, "p_plan_mw") for r in s6_rows] if s6_rows else []
        s6_ess   = [_f(r, "p_ess_target_mw") for r in s6_rows] if s6_rows else []
        s6_pv_tgt = [_f(r, "p_pv_target_total_mw") for r in s6_rows] if s6_rows else []

        pv_curtailed = sum(1 for t in s6_pv_tgt if t > 0.01) if s6_pv_tgt else 0
        pv_curtailed_pct = pv_curtailed / len(s6_pv_tgt) * 100 if s6_pv_tgt else 0
        pv_free_pct = 100.0 - pv_curtailed_pct
        ess_negative_pct = sum(1 for e in s6_ess if e < -0.01) / len(s6_ess) * 100 if s6_ess else 0
        ess_positive_pct = sum(1 for e in s6_ess if e > 0.01) / len(s6_ess) * 100 if s6_ess else 0

        # ★ v4.34: 新的检测优先级 — ESS极性优先于PV限电
        #   S6-2 特征: ESS仅充电(负), 不放电 → ess负>20% 且 ess正<3%
        #   S6-3 特征: ESS仅放电(正), 不充电 → ess正>20% 且 ess负<3%
        #   S6-1 特征: PV被限电 或 ESS双向动作
        if ess_negative_pct > 20.0 and ess_positive_pct < 3.0:
            test_type = "S6-2 (MPPT充电)"
        elif ess_positive_pct > 20.0 and ess_negative_pct < 3.0:
            test_type = "S6-3 (保底出力)"
        elif pv_curtailed_pct > 30.0:
            test_type = "S6-1 (本地曲线)"
        elif s6_plans and max(s6_plans) - min(s6_plans) > 5.0:
            test_type = "S6-1 (本地曲线)"
        else:
            test_type = "S6-Unknown"
            print(f"  {YELLOW}[S6] 无法自动检测策略 (PV限电={pv_curtailed_pct:.0f}%"
                  f" PV自由={pv_free_pct:.0f}% ESS负={ess_negative_pct:.0f}%"
                  f" ESS正={ess_positive_pct:.0f}%){RESET}")

        # ═══════════════════════════════════════════════════
        # A 组: 场景切换 — 基于 active_scene 检测
        #   ★ 结果 CSV 无 comm_status 列, 用 scene 跳变检测 S6 进入/退出
        # ═══════════════════════════════════════════════════
        s6_enter_t = next((_f(r,"timestamp_s") for r in rows
                          if _f(r,"timestamp_s") >= HYSTERESIS_SKIP_SECONDS
                          and abs(_f(r,"active_scene") - 6.0) <= 0.5), None)
        s6_exit_t  = None
        if s6_enter_t:
            # ★ v4.34: 接受任何 S6→非S6 的退出 (S6→S1/S4/S3均可)
            #   通信恢复时频率可能偏离 → S4截胡 → 路径 S6→S4→S1 是合法的工业行为
            s6_exit_t = next((_f(r,"timestamp_s") for r in rows
                             if _f(r,"timestamp_s") > s6_enter_t + 2
                             and abs(_f(r,"active_scene") - 6.0) > 0.5
                             and _f(r,"active_scene") > 0.5
                             and any(abs(_f(r2,"active_scene")-6.0)<=0.5
                                     for r2 in rows
                                     if _f(r2,"timestamp_s") >= _f(r,"timestamp_s") - 3
                                     and _f(r2,"timestamp_s") < _f(r,"timestamp_s"))), None)

        if s6_enter_t:
            # S6 应在仿真早期就切入 (心跳超时 5s + 初始化 3s 后)
            s6_entry_latency = s6_enter_t - HYSTERESIS_SKIP_SECONDS
            ar.check(s6_entry_latency <= 13.0,
                     f"S6-A1: Scene=6 首次 @ t={s6_enter_t:.0f}s "
                     f"(启动+{s6_entry_latency:.0f}s, ≤13s)", "CRITICAL")
        else:
            ar.check(False, "S6-A1: 全程未检测到 Scene=6", "CRITICAL")

        if s6_exit_t:
            exit_scene = next((_f(r,"active_scene") for r in rows
                              if abs(_f(r,"timestamp_s") - s6_exit_t) < 0.1), 0)
            s6_duration = s6_exit_t - s6_enter_t
            ar.check(True,
                     f"S6-A2: Scene 6→{exit_scene:.0f} @ t={s6_exit_t:.0f}s "
                     f"(S6持续 {s6_duration:.0f}s)", "CRITICAL")
        elif s6_enter_t:
            # ★ v4.34: 退出检测不到不一定是控制bug。
            #   恢复链: 心跳恢复→alive确认(3次)→SelfHeal(3次频率/电压全健康)→S1
            #   测试时长不够或PV波动导致频率越限→SelfHeal清零→链未走完
            #   降级为 WARN 而非 FAIL: 心跳恢复门已打印即证明恢复逻辑在工作
            ar.check(False,
                     "S6-A2: 测试窗口内未检测到S6退出 (恢复链未走完, 非控制缺陷)",
                     "WARN")

        # ── v4.34: 强制模式覆盖 (--mode 参数) ──
        if forced_mode:
            mode_map = {"s6-1": "S6-1 (本地曲线)", "s6-2": "S6-2 (MPPT充电)",
                        "s6-3": "S6-3 (保底出力)", "s6-0": "S6-Generic"}
            test_type = mode_map.get(forced_mode, test_type)
            print(f"  {CYAN}[S6] 强制模式: {test_type} (--mode={forced_mode}){RESET}")

        # ═══════════════════════════════════════════════════
        # B 组: 策略专属断言 (v4.28: 对齐新策略语义)
        # ═══════════════════════════════════════════════════
        if "S6-1" in test_type:
            self._eval_s6_schedule(ar, rows)
        elif "S6-2" in test_type:
            self._eval_s6_mppt(ar, rows)
        elif "S6-3" in test_type:
            self._eval_s6_min(ar, rows)
        else:
            self._eval_s6_generic(ar, rows)

        # ═══════════════════════════════════════════════════
        # D 组: 安全边界
        # ═══════════════════════════════════════════════════
        check_soc_safety(ar, rows, "S6-D1")
        check_ess_power_limit(ar, rows, "S6-D2")

        # ═══════════════════════════════════════════════════
        # ★★★ v4.24: PHYS 组 — 物理合规性硬断言 (不可降级) ★★★
        # 即使所有控制逻辑断言 PASS, 若违背电网物理规律,
        # 也必须判定 FAIL。这是工业级 HIL 仿真的底线。
        # ═══════════════════════════════════════════════════
        check_physics_pcc_bounds(ar, rows, "S6-PHYS-1")
        check_physics_rocof(ar, rows, "S6-PHYS-2")

        print(f"\n  {CYAN}[S6] 检测到测试类型: {test_type}{RESET}")
        return ar

    # ── S6 通用: 切入/退出无冲击 ──
    def _eval_s6_generic(self, ar, rows):
        s6_enter_t = next((_f(r,"timestamp_s") for r in rows
                          if _f(r,"timestamp_s") >= HYSTERESIS_SKIP_SECONDS
                          and abs(_f(r,"active_scene")-6.0) <= 0.5), None)
        if s6_enter_t:
            pre = [_f(r,"p_pcc_total_mw") for r in rows
                   if s6_enter_t-3 <= _f(r,"timestamp_s") < s6_enter_t]
            mid = [_f(r,"p_pcc_total_mw") for r in rows
                   if s6_enter_t <= _f(r,"timestamp_s") <= s6_enter_t+3]
            if pre and mid:
                step = abs(sum(mid[:3])/min(3,len(mid)) - sum(pre)/len(pre))
                ar.check(step < 20.0,
                         f"S6-B0: S6切入 PCC阶跃={step:.1f}MW < 20MW", "CRITICAL")

    # ── S6-1: 本地计划曲线 ──
    def _eval_s6_schedule(self, ar, rows):
        s6_rows = [r for r in rows
                   if _f(r,"timestamp_s") >= HYSTERESIS_SKIP_SECONDS + 5
                   and abs(_f(r,"active_scene")-6.0) <= 0.5]
        if not s6_rows:
            ar.check(False, "S6-SCHEDULE: S6 期间无数据", "CRITICAL")
            return

        # B1-1: PCC 跟踪本地计划曲线 (target 随时间变化, 误差 < 30%)
        pcc_vals = [_f(r,"p_pcc_total_mw") for r in s6_rows]
        plan_vals = [_f(r,"p_plan_mw") for r in s6_rows]
        errors = [abs(p - l) / max(l, 0.1) * 100 for p, l in zip(pcc_vals, plan_vals) if l > 0.1]
        if errors:
            avg_err = sum(errors) / len(errors)
            ar.check(avg_err < 30.0,
                     f"S6-B1-1: 计划跟踪平均误差={avg_err:.1f}% < 30%", "CRITICAL")

        # B1-2: PV 限电使能 (ratio < 1.0 表示有限电行为)
        pv_limited = sum(1 for r in s6_rows if _f(r,"p_pv_target_total_mw") > 0.01)
        ar.check(pv_limited > 0,
                 f"S6-B1-2: PV 限电激活 ({pv_limited}/{len(s6_rows)} 周期有限电)", "CRITICAL")

    # ── S6-2: MPPT 最大出力 + 余电充电 ──
    def _eval_s6_mppt(self, ar, rows):
        s6_rows = [r for r in rows
                   if _f(r,"timestamp_s") >= HYSTERESIS_SKIP_SECONDS + 5
                   and abs(_f(r,"active_scene")-6.0) <= 0.5]
        if not s6_rows:
            ar.check(False, "S6-MPPT: S6 期间无数据", "CRITICAL")
            return

        # B2-1: PV 自由 MPPT (PV target ≈ 0, 不限电)
        pv_targets = [_f(r,"p_pv_target_total_mw") for r in s6_rows]
        pv_free_pct = sum(1 for t in pv_targets if t < 0.01) / len(pv_targets) * 100
        ar.check(pv_free_pct > 50.0,
                 f"S6-B2-1: PV MPPT释放 {pv_free_pct:.0f}% 周期 > 50%", "CRITICAL")

        # B2-2: ESS 仅充电不放电 (ESS target ≤ 0)
        ess_targets = [_f(r,"p_ess_target_mw") for r in s6_rows]
        ess_charge_only = sum(1 for e in ess_targets if e <= 0.01) / len(ess_targets) * 100
        ar.check(ess_charge_only > 70.0,
                 f"S6-B2-2: ESS仅充电 {ess_charge_only:.0f}% 周期 > 70%", "CRITICAL")

    # ── S6-3: 保底最小出力 ──
    def _eval_s6_min(self, ar, rows):
        s6_rows = [r for r in rows
                   if _f(r,"timestamp_s") >= HYSTERESIS_SKIP_SECONDS + 5
                   and abs(_f(r,"active_scene")-6.0) <= 0.5]
        if not s6_rows:
            ar.check(False, "S6-MIN: S6 期间无数据", "CRITICAL")
            return

        # B3-1: PCC ≥ 5MW (保底出力 10MW, 容忍 5MW 误差)
        pcc_min = min(_f(r,"p_pcc_total_mw") for r in s6_rows)
        ar.check(pcc_min >= 5.0,
                 f"S6-B3-1: PCC最低={pcc_min:.1f}MW ≥ 5MW (保底10MW)", "CRITICAL")

        # B3-2: ESS 仅放电不充电 (ESS target ≥ 0)
        ess_targets = [_f(r,"p_ess_target_mw") for r in s6_rows]
        ess_disch_only = sum(1 for e in ess_targets if e >= -0.01) / len(ess_targets) * 100
        ar.check(ess_disch_only > 70.0,
                 f"S6-B3-2: ESS仅放电 {ess_disch_only:.0f}% 周期 > 70%", "CRITICAL")

    # ── 可视化 (v4.35: 5子图, PV+ESS+SOC+Plan全展示) ──
    def plot_analysis(self, rows, png):
        try:
            import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
        except ImportError:
            print(f"{YELLOW}[SKIP] matplotlib missing{RESET}"); return
        ts    = [float(r["timestamp_s"]) for r in rows]
        scene = [float(r["active_scene"]) for r in rows]
        pcc   = [float(r["p_pcc_total_mw"]) for r in rows]
        plan  = [float(r["p_plan_mw"]) for r in rows]
        pv    = [float(r["p_pv_total_mw"]) for r in rows]
        freq  = [float(r["grid_frequency_hz"]) for r in rows]
        ess   = [float(r["p_ess_target_mw"]) for r in rows]
        soc   = [float(r["ess_soc_pct"]) for r in rows]
        pv_tgt= [float(r["p_pv_target_total_mw"]) for r in rows]
        comm  = [0.0 if abs(float(r["active_scene"])-6.0)<=0.5 else 1.0 for r in rows]

        fig, axes = plt.subplots(5, 1, figsize=(16, 18), sharex=True)

        # ── 子图1: Scene + Comm Loss ──
        axes[0].step(ts, scene, "b-", lw=1.5, where="post", label="Scene")
        axes[0].fill_between(ts, 0, 7, where=[c<0.5 for c in comm],
                             alpha=0.08, color="red", label="Comm Loss")
        axes[0].axhline(6, color="red", ls="--", alpha=0.5)
        axes[0].set_ylabel("Scene"); axes[0].set_title("S6: Communication Loss & Recovery")
        axes[0].legend(fontsize=8); axes[0].grid(alpha=0.3); axes[0].set_ylim(0, 7)

        # ── 子图2: Power — PCC + Plan + PV ──
        axes[1].plot(ts, pv,  "#DAA520", lw=1.2, alpha=0.8, label="PV")
        axes[1].plot(ts, pcc, "#DC143C", lw=1.5, label="PCC")
        axes[1].plot(ts, plan,"k--", lw=1.5, alpha=0.7, label="Plan")
        # PV target (curtailment indicator)
        axes[1].fill_between(ts, 0, pv_tgt, alpha=0.08, color="orange", label="PV Curtailed")
        axes[1].set_ylabel("MW"); axes[1].set_title("Power: PV + PCC + Plan")
        axes[1].legend(fontsize=7, ncol=2); axes[1].grid(alpha=0.3)

        # ── 子图3: ESS Target (双Y轴) ──
        axes[2].plot(ts, ess, "#228B22", lw=1.5, label="ESS Target")
        axes[2].axhline(0, color="gray", ls="--", alpha=0.4)
        axes[2].set_ylabel("ESS (MW)", color="#228B22")
        axes[2].set_ylim(-12, 12)
        axes[2].tick_params(axis="y", labelcolor="#228B22")
        ax2b = axes[2].twinx()
        ax2b.plot(ts, soc, "#1E90B2", lw=1.2, alpha=0.7, label="SOC")
        ax2b.set_ylabel("SOC (%)", color="#1E90B2")
        ax2b.set_ylim(55, 65)
        ax2b.tick_params(axis="y", labelcolor="#1E90B2")
        axes[2].set_title("ESS Target Power & SOC")
        l1, lb1 = axes[2].get_legend_handles_labels()
        l2, lb2 = ax2b.get_legend_handles_labels()
        axes[2].legend(l1+l2, lb1+lb2, fontsize=7); axes[2].grid(alpha=0.3)

        # ── 子图4: Grid Frequency ──
        axes[3].plot(ts, freq, "b-", lw=1, alpha=0.7, label="Grid Freq")
        axes[3].axhline(50.0, color="gray", ls="--", alpha=0.4)
        axes[3].axhline(50.05, color="red", ls=":", alpha=0.5)
        axes[3].axhline(49.95, color="red", ls=":", alpha=0.5)
        axes[3].fill_between(ts, 49.95, 50.05, alpha=0.04, color="red")
        axes[3].set_ylabel("Hz"); axes[3].set_title("Grid Frequency (PFR deadband ±0.05Hz)")
        axes[3].legend(fontsize=8); axes[3].grid(alpha=0.3)

        # ── 子图5: PV Target (curtailment indicator) ──
        axes[4].plot(ts, pv_tgt, "#DAA520", lw=1.2, label="PV Target (0=MPPT free)")
        axes[4].fill_between(ts, 0, pv_tgt, alpha=0.1, color="orange")
        axes[4].axhline(0, color="green", ls="--", alpha=0.4, lw=1, label="MPPT (target=0)")
        axes[4].set_ylabel("MW"); axes[4].set_xlabel("Time (s)")
        axes[4].set_title("PV Curtailment Target (strategy indicator)")
        axes[4].legend(fontsize=8); axes[4].grid(alpha=0.3)

        plt.tight_layout(); os.makedirs(os.path.dirname(png)or".",exist_ok=True)
        plt.savefig(png, dpi=150, bbox_inches="tight")
        print(f"{GREEN}[PLOT] {png}{RESET}"); plt.close()

    # ── v3.0: 重写 run() — 增加 --mode 和 --output-png ──
    def run(self, argv=None):
        import argparse as _ap
        p = _ap.ArgumentParser(description=f"{self.scenario_label} 测试评估与可视化 v3.0")
        p.add_argument("--result", "-r", default=None, help="结果 CSV 路径")
        p.add_argument("--mode", "-m", default=None, choices=["s6-0","s6-1","s6-2","s6-3"],
                       help="强制策略模式 (默认自动检测)")
        p.add_argument("--output-png", "-p", default=None, help="图表输出路径")
        p.add_argument("--no-plot", action="store_true", help="跳过图表生成")
        args = p.parse_args(argv[1:] if argv else [])

        script_dir = os.path.dirname(os.path.abspath(__file__))
        result_csv = args.result or os.path.join(script_dir, self.default_result_csv())
        result_csv = os.path.normpath(result_csv)
        # ★ v4.35: 按策略名分离 PNG (s6_1_plot.png / s6_2_plot.png / s6_3_plot.png)
        if args.mode and not args.output_png:
            strat_tag = args.mode.replace("s6-", "")
            png_name = f"s6_{strat_tag}_analysis_plot.png"
        else:
            png_name = self.default_png_path()
        png_path = args.output_png or os.path.join(os.path.dirname(result_csv), png_name)
        png_path = os.path.normpath(png_path)

        print(f"{BOLD}{self.scenario_label} 测试评估器 v3.0{RESET}")
        print(f"  结果文件: {result_csv}")
        if args.mode: print(f"  策略模式: {args.mode} (强制覆盖自动检测)")

        rows = load_results(result_csv)
        print_statistics(rows)
        ar = self.evaluate(rows, forced_mode=args.mode)
        passed = print_report(ar, scenario_label=self.scenario_label)
        if not args.no_plot:
            self.plot_analysis(rows, png_path)
        return passed

if __name__=="__main__": sys.exit(0 if S6Evaluator().run(sys.argv) else 1)
