// ============================================================
// s2_runner.cpp — S2 云遮快速波动 HIL 测试驱动
// ============================================================
// 云遮场景: 辐照度快速波动导致光伏出力不稳定。
// PV 物理模型受辐照度约束: pv_theory = pv_inv_power * (irradiance / 1000.0)
// ============================================================

#include "common_runner.h"

static const double IRRADIANCE_BASE = 1000.0;

class S2Runner : public BaseRunner {
public:
    S2Runner() : BaseRunner("S2 云遮快速波动") {}

protected:
    std::string DefaultInputCSV()  override { return "../../test_cases/s2_cloud/s2_cloud_test.csv"; }
    std::string DefaultOutputCSV() override { return "../../test_reports/s2_cloud/s2_execution_result.csv"; }

    int ComputeStatusCode(const ResultRow& r, const CsvInputRow& row) override {
        int sc = 0;
        if (r.active_scene != 2.0) sc = 2;
        if (r.tracking_error_pct > 5.0) sc = 3;
        if (std::abs(row.grid_frequency_hz - 50.0) > 0.05) sc = 1;
        return sc;
    }

    void OnPhysicsStep(double ess_target, double pv1_tgt, double pv2_tgt, double pv3_tgt,
                       const CsvInputRow& row,
                       double& ess_act, double& ess_soc,
                       double& pv1_act, double& pv2_act, double& pv3_act) override
    {
        double dt = SIM_DT_MS * CONTROL_CYCLE_STEPS;

        // ESS 一阶惯性 + SOC 能量积分
        ess_act = first_order_lag(ess_target, ess_act, ESS_RESPONSE_TIME_MS, dt);
        ess_act = (std::max)(-ESS_MAX_POWER_MW, (std::min)(ESS_MAX_POWER_MW, ess_act));
        double e_mwh = ess_act * (dt / 1000.0 / 3600.0);
        ess_soc -= e_mwh / ESS_CAPACITY_MWH * 100.0;
        ess_soc = (std::max)(0.0, (std::min)(100.0, ess_soc));

        // PV 功率受辐照度约束: pv_theory = 逆变器基线 × (实时辐照度 / 1000 W/m²)
        double irr_ratio = row.irradiance_w_per_m2 / IRRADIANCE_BASE;

        double pv1_theory = row.pv_inv1_power_mw * irr_ratio;
        double pv2_theory = row.pv_inv2_power_mw * irr_ratio;
        double pv3_theory = row.pv_inv3_power_mw * irr_ratio;

        double pv1 = (pv1_tgt > 0.01) ? (std::min)(pv1_tgt, pv1_theory) : pv1_theory;
        double pv2 = (pv2_tgt > 0.01) ? (std::min)(pv2_tgt, pv2_theory) : pv2_theory;
        double pv3 = (pv3_tgt > 0.01) ? (std::min)(pv3_tgt, pv3_theory) : pv3_theory;

        pv1_act = first_order_lag(pv1, pv1_act, PV_RESPONSE_TIME_MS, dt);
        pv2_act = first_order_lag(pv2, pv2_act, PV_RESPONSE_TIME_MS, dt);
        pv3_act = first_order_lag(pv3, pv3_act, PV_RESPONSE_TIME_MS, dt);
    }
};

int main(int argc, char** argv) {
    return S2Runner().Run(argc, argv);
}
