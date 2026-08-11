#!/usr/bin/env python3
# s1_evaluator.py — S1 稳态基线跟踪 评估器
import sys, os
from common_evaluator import (
    GREEN,RED,YELLOW,CYAN,RESET,BOLD, TRACKING_ERROR_MAX_PCT, TRANSITION_MAX_ERR_PCT,
    AssertionResult, BaseEvaluator,
    check_pfr_all_time, check_scene_identity, check_soc_safety, check_ess_power_limit,
)
STEADY_1_S,STEADY_1_E=5.0,30.0; RAMP_S,RAMP_E=30.0,50.0; STEADY_2_S,STEADY_2_E=55.0,60.0

class S1Evaluator(BaseEvaluator):
    def __init__(self): super().__init__("S1", expected_scene=1)
    def default_result_csv(self) -> str: return "../../test_reports/s1_baseline/s1_execution_result.csv"
    def evaluate(self, rows):
        ar=AssertionResult(); f=lambda r,k:float(r.get(k,0))
        check_pfr_all_time(ar,rows,"S1-A1"); check_scene_identity(ar,rows,expected_scene=1,assertion_id="S1-A2")
        for sid,ss,se in [("S1-B1",STEADY_1_S,STEADY_1_E),("S1-B2",STEADY_2_S,STEADY_2_E)]:
            errs=[f(r,"tracking_error_pct") for r in rows if ss<=f(r,"timestamp_s")<=se]
            if errs: ar.check(sorted(errs)[len(errs)//2]<TRACKING_ERROR_MAX_PCT, f"{sid}: 稳态段 [{ss}-{se}s] 中位数={sorted(errs)[len(errs)//2]:.3f}%","CRITICAL")
            else: ar.check(False,f"{sid}: 无数据","CRITICAL")
        re=[f(r,"tracking_error_pct") for r in rows if RAMP_S<=f(r,"timestamp_s")<=RAMP_E]
        if re: ar.check(max(re)<TRANSITION_MAX_ERR_PCT, f"S1-C1: 微调段 [{RAMP_S}-{RAMP_E}s] max={max(re):.2f}%","WARN")
        else: ar.details.append((True,"WARN","S1-C1: 无数据"))
        check_soc_safety(ar,rows,"S1-D1"); check_ess_power_limit(ar,rows,"S1-D2")
        return ar
    def plot_analysis(self,rows,png):
        try: import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
        except ImportError: print(f"{YELLOW}[SKIP] matplotlib missing{RESET}"); return
        ts=[float(r["timestamp_s"]) for r in rows]
        freq=[float(r["grid_frequency_hz"]) for r in rows]; p_plan=[float(r["p_plan_mw"]) for r in rows]
        p_pv=[float(r["p_pv_total_mw"]) for r in rows]; p_ess=[float(r["p_ess_actual_mw"]) for r in rows]
        p_grid=[float(r["p_pcc_total_mw"]) for r in rows]; soc=[float(r["ess_soc_pct"]) for r in rows]
        status=[int(float(r["status_code"])) for r in rows]
        fig,(ax1,ax2,ax3)=plt.subplots(3,1,figsize=(16,12),sharex=True)
        ax1.plot(ts,freq,"b-",lw=1.2,label="Grid Freq"); ax1.axhline(50,color="gray",ls="--",alpha=.4)
        ax1.axhline(50.05,color="red",ls=":",alpha=.6); ax1.axhline(49.95,color="red",ls=":",alpha=.6)
        ax1.fill_between(ts,49.95,50.05,alpha=.04,color="red"); ax1.set_ylabel("Hz"); ax1.set_title("Grid Frequency")
        ax1.legend(fontsize=8); ax1.grid(alpha=.3); ax1.set_ylim(49.9,50.1)
        ax2.plot(ts,p_plan,"k--",lw=2,alpha=.8,label="Plan"); ax2.plot(ts,p_pv,"#DAA520",lw=1.5,alpha=.9,label="PV")
        ax2.plot(ts,p_grid,"#DC143C",lw=1.5,alpha=.9,label="PCC")
        ax2.set_ylabel("MW (PV/PCC)"); ax2.set_title("Power Tracking"); ax2.grid(alpha=.3)
        ax2.axvspan(RAMP_S,RAMP_E,alpha=.06,color="orange")
        ax2b=ax2.twinx(); ax2b.plot(ts,p_ess,"#228B22",lw=1.8,alpha=.9,label="ESS"); ax2b.set_ylabel("MW (ESS)",color="#228B22")
        ax2b.tick_params(axis="y",labelcolor="#228B22"); ax2b.set_ylim(-12,12)
        l1,lb1=ax2.get_legend_handles_labels(); l2,lb2=ax2b.get_legend_handles_labels()
        ax2.legend(l1+l2,lb1+lb2,fontsize=8,ncol=2)
        ax3a=ax3; ax3a.plot(ts,soc,"-",color="#1E90B2",lw=1.8,label="SOC %"); ax3a.set_ylabel("SOC %",color="#1E90B2")
        ax3a.tick_params(axis="y",labelcolor="#1E90B2"); ax3b=ax3a.twinx()
        ax3b.step(ts,status,"-",color="#8B0000",lw=1.5,where="post",label="Status")
        ax3b.set_ylabel("Status",color="#8B0000"); ax3b.set_ylim(-.5,5.5); ax3b.set_yticks([0,1,2,3])
        ax3b.set_yticklabels(["0:OK","1:Freq","2:Scene","3:Power"]); ax3a.set_title("ESS SOC & Status")
        l1,lb1=ax3a.get_legend_handles_labels(); l2,lb2=ax3b.get_legend_handles_labels()
        ax3a.legend(l1+l2,lb1+lb2,fontsize=8); ax3a.grid(alpha=.3); ax3.set_xlabel("Time (s)")
        plt.tight_layout(); os.makedirs(os.path.dirname(png)or".",exist_ok=True)
        plt.savefig(png,dpi=150,bbox_inches="tight"); print(f"{GREEN}[PLOT] {png}{RESET}"); plt.close()
if __name__=="__main__": sys.exit(0 if S1Evaluator().run(sys.argv) else 1)
