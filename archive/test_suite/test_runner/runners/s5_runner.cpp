// ============================================================
// s5_runner.cpp — S5 光储联合优化限电调度 HIL 测试驱动 (v2.0)
// ============================================================
// ★ v2.0 修复: 重写 OnInjectTelemetry — 直接注入 CSV PV 功率,
//   绕过 OnPhysicsStep 中 AGC TARGET_POWER 优先导致的闭环死锁。
//   根因: 旧 Runner 的 PV 物理模型在 pv1_tgt > 0.01 时优先使用
//   AGC 写入的 TARGET_POWER, 忽略 CSV 爬坡数据, 形成稳定平衡锁死。
// ============================================================

#include "common_runner.h"

class S5Runner : public BaseRunner {
public:
    S5Runner() : BaseRunner("S5 光储联合优化限电调度") {}

protected:
    std::string DefaultInputCSV()  override {
        return "../../test_cases/s5_joint/s5_soc_test.csv";
    }
    std::string DefaultOutputCSV() override {
        return "../../test_reports/s5_joint/s5_execution_result.csv";
    }

    // ★★★ v2.0: 直接注入 CSV PV 功率, 绕过物理模型 AGC-target 锁死 ★★★
    // 旧逻辑: pv1/pv2/pv3 来自 m_pv1_actual_mw (物理状态),
    //         而物理状态被 OnPhysicsStep 中 "pv1_tgt>0.01→使用AGC目标" 锁死
    // 新逻辑: S5 激励剖面要求 PV 按 CSV 爬坡 (80→120MW),
    //         在 AGC 未进入 S5 前, PV 应自由运行于 MPPT 模式
    void OnInjectTelemetry(int /*step*/, CsvInputRow& row,
                           double& pv1, double& pv2, double& pv3,
                           double& /*ess*/, int& /*hb*/) override {
        // 直接注入 CSV 行中的 PV 逆变器功率
        // 这确保 AGC 看到的 tele.pv_total_power 反映激励剖面的 PV 爬坡
        pv1 = row.pv_inv1_power_mw;
        pv2 = row.pv_inv2_power_mw;
        pv3 = row.pv_inv3_power_mw;
    }

    int ComputeStatusCode(const ResultRow& r, const CsvInputRow& row) override {
        int sc = 0;
        if (r.active_scene != 5.0) sc = 2;
        if (r.tracking_error_pct > 5.0) sc = 3;
        if (std::abs(row.grid_frequency_hz - 50.0) > 0.05) sc = 1;
        return sc;
    }
};

int main(int argc, char** argv) {
    return S5Runner().Run(argc, argv);
}
