#!/usr/bin/env python3
# s3_evaluator.py — S3 调度指令阶跃 评估器 (v2.1 — 对齐 v4.17 控制逻辑)
import sys, os
from common_evaluator import (
    GREEN,RED,YELLOW,CYAN,RESET,BOLD, TRACKING_ERROR_MAX_PCT, AssertionResult, BaseEvaluator,
    check_pfr_all_time, check_scene_identity, check_soc_safety, check_ess_power_limit,
)

# ★ v4.35: 对齐 generator v3.0 (140s, 上行20s/下行80s)
STEP1_T, STEP2_T = 20.0, 80.0
S3_ENTRY_T  = 22.0   # 上行切入 (20 + 2s DE迟滞)
S3_ENTRY_T2 = 82.0   # 下行切入 (80 + 2s DE迟滞)

S3_TR_MAX = 7.0    # 上升时间: PCC 80→107MW
S3_TS_MAX = 15.0   # 调节时间: PCC 进入 ±2% 稳态带

class S3Evaluator(BaseEvaluator):
    def __init__(self): super().__init__("S3", expected_scene=3)

    def default_result_csv(self) -> str:
        return "../../test_reports/s3_step/s3_execution_result.csv"

    def evaluate(self, rows):
        ar = AssertionResult(); f = lambda r,k: float(r.get(k,0))
        check_pfr_all_time(ar, rows, "S3-A1")
        # S3-A2: Scene=3 仅在阶跃响应窗口内断言 (非全过程!)
        # 窗口1: 阶跃1后 17-32s (15+2DE迟滞=17; hold 15s → 17+15=32)
        # 窗口2: 阶跃2后 52-67s (50+2=52; hold 15s → 52+15=67)
        # ★ v4.35: 窗口对齐 hold=25周期 (22+25=47s, 82+25=107s)
        w1 = [r for r in rows if 22.0 <= f(r,"timestamp_s") <= 46.0]
        w2 = [r for r in rows if 82.0 <= f(r,"timestamp_s") <= 106.0]
        bad1 = [r for r in w1 if int(f(r,"active_scene")) != 3]
        bad2 = [r for r in w2 if int(f(r,"active_scene")) != 3]
        total_bad = len(bad1) + len(bad2)
        ar.check(total_bad <= 3,  # 容忍 3 次边界抖动 (含 SelfHeal scene=0)
            f"S3-A2: Scene=3 in step windows [22-46s]+[82-106s] (违规 {total_bad} 次)", "CRITICAL")
        # B1: rise time Tr (从 S3 实际切入起算, 对齐 DE 2周期迟滞)
        t0 = next((f(r,"timestamp_s") for r in rows if f(r,"timestamp_s")>=S3_ENTRY_T), S3_ENTRY_T)
        t90 = next((f(r,"timestamp_s") for r in rows if f(r,"timestamp_s")>=S3_ENTRY_T and f(r,"p_pcc_total_mw")>=107.0), None)
        if t90: ar.check(t90-t0 <= S3_TR_MAX, f"S3-B1: Tr={t90-t0:.2f}s <= {S3_TR_MAX}s (80->107MW)", "CRITICAL")
        else: ar.check(False, "S3-B1: PCC not reaching 107MW", "CRITICAL")
        # B2: settling time Ts
        settled = [f(r,"timestamp_s") for r in rows if f(r,"timestamp_s")>=S3_ENTRY_T and 107.8<=f(r,"p_pcc_total_mw")<=112.2]
        if settled: ar.check(settled[0]-S3_ENTRY_T <= S3_TS_MAX, f"S3-B2: Ts={settled[0]-S3_ENTRY_T:.2f}s <= {S3_TS_MAX}s (+-2% of 110MW)", "CRITICAL")
        else: ar.check(False, "S3-B2: not settled", "CRITICAL")
        # B3: overshoot — ★跳过 S3 切入后前 3 周期 (场景切换过渡窗口)
        S3_GRACE_START = S3_ENTRY_T + 3.0   # 给 ESS 3s 从任何前序场景残留恢复
        pcc_step1 = [f(r,"p_pcc_total_mw") for r in rows if S3_GRACE_START<=f(r,"timestamp_s")<=S3_ENTRY_T+10]
        if pcc_step1:
            ov = (max(pcc_step1)-110.0)/30.0*100
            ar.check(ov < 10.0, f"S3-B3: overshoot={ov:.1f}% < 10% (跳过前 3s 过渡)", "CRITICAL")
        else: ar.check(False, "S3-B3: no data", "CRITICAL")
        # C1a: upward step (PV=80MW, plan=110MW → gap=+30MW 缺口)
        #      ★ v2.1: ESS 必须放电 (positive) 顶补缺口, 趋势或绝对值均可
        eb1 = [f(r,"p_ess_target_mw") for r in rows if S3_ENTRY_T-2<=f(r,"timestamp_s")<S3_ENTRY_T]
        ea1 = [f(r,"p_ess_target_mw") for r in rows if S3_ENTRY_T<f(r,"timestamp_s")<=S3_ENTRY_T+5]
        if eb1 and ea1:
            mb1, ma1 = sum(eb1)/len(eb1), sum(ea1)/len(ea1)
            trending_discharge = (ma1 > mb1 + 0.5)  # 朝放电方向移动 >0.5MW
            ar.check(trending_discharge or ma1 > 0.0,
                f"S3-C1a: upward step ESS放电 (before={mb1:.1f}MW, after={ma1:.1f}MW, Δ={ma1-mb1:+.1f}MW)", "CRITICAL")
        else: ar.check(False, "S3-C1a: no data", "CRITICAL")
        # C1b: downward step (PV=55MW, plan=50MW → gap=-5MW 盈余)
        #      ★ v2.1: ESS 应充电 (negative) 吸收盈余, 趋势或绝对值均可
        eb2 = [f(r,"p_ess_target_mw") for r in rows if S3_ENTRY_T2-2<=f(r,"timestamp_s")<S3_ENTRY_T2]
        ea2 = [f(r,"p_ess_target_mw") for r in rows if S3_ENTRY_T2<f(r,"timestamp_s")<=S3_ENTRY_T2+5]
        if eb2 and ea2:
            mb2, ma2 = sum(eb2)/len(eb2), sum(ea2)/len(ea2)
            trending_charge = (ma2 < mb2 - 0.5)  # 朝充电方向移动 >0.5MW
            ar.check(trending_charge or ma2 < -0.5,
                f"S3-C1b: downward step ESS充电 (before={mb2:.1f}MW, after={ma2:.1f}MW, Δ={ma2-mb2:+.1f}MW)", "CRITICAL")
        else: ar.check(False, "S3-C1b: no data", "CRITICAL")
        check_soc_safety(ar, rows, "S3-D1")
        check_ess_power_limit(ar, rows, "S3-D2")
        return ar

    def plot_analysis(self, rows, png):
        try: import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
        except ImportError: print(f"{YELLOW}[SKIP] matplotlib missing{RESET}"); return
        ts=[float(r["timestamp_s"]) for r in rows]
        plan=[float(r["p_plan_mw"]) for r in rows]; pcc=[float(r["p_pcc_total_mw"]) for r in rows]
        ess=[float(r["p_ess_target_mw"]) for r in rows]; pv=[float(r["p_pv_total_mw"]) for r in rows]
        soc=[float(r["ess_soc_pct"]) for r in rows]
        fig,axes=plt.subplots(3,1,figsize=(16,12),sharex=True)
        axes[0].plot(ts,plan,"k--",lw=2,alpha=.8,label="Plan"); axes[0].plot(ts,pcc,"#DC143C",lw=1.5,label="PCC")
        axes[0].plot(ts,pv,"#DAA520",lw=1,alpha=.7,label="PV")
        axes[0].axvline(STEP1_T,color="blue",ls=":",alpha=.6); axes[0].axvline(STEP2_T,color="red",ls=":",alpha=.6)
        axes[0].set_ylabel("MW"); axes[0].set_title("S3: Plan Step Response"); axes[0].legend(fontsize=8); axes[0].grid(alpha=.3)
        axes[1].plot(ts,ess,"#228B22",lw=1.5,label="ESS Target"); axes[1].axhline(0,color="gray",ls="--",alpha=.4)
        axes[1].axvspan(STEP2_T-3,STEP2_T,alpha=.08,color="orange",label="before"); axes[1].axvspan(STEP2_T+1,STEP2_T+4,alpha=.08,color="green",label="after")
        axes[1].set_ylabel("MW"); axes[1].set_title("ESS Power (direction check windows)"); axes[1].legend(fontsize=8); axes[1].grid(alpha=.3)
        axes[2].plot(ts,soc,"#1E90B2",lw=1.5,label="SOC"); axes[2].set_ylabel("%"); axes[2].set_xlabel("Time (s)")
        axes[2].set_title("ESS SOC"); axes[2].legend(fontsize=8); axes[2].grid(alpha=.3)
        plt.tight_layout(); os.makedirs(os.path.dirname(png)or".",exist_ok=True)
        plt.savefig(png,dpi=150,bbox_inches="tight"); print(f"{GREEN}[PLOT] {png}{RESET}"); plt.close()

if __name__=="__main__": sys.exit(0 if S3Evaluator().run(sys.argv) else 1)
