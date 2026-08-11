// ============================================================
// s4_runner.cpp — S4 一次调频 (PFR) 响应 HIL 测试驱动
// ============================================================
// PFR 场景: 电网频率偏离 50Hz, 验证 AGC 按 Droop 特性提供一次调频功率。
// Droop 公式: ΔP = -(f - 50) / 50 / 0.04 * 120 = -60.0 * (f - 50) MW
// 注意: S4 场景下频率偏差是预期的, 不应标记为 status_code=1。
// ============================================================

#include "common_runner.h"

class S4Runner : public BaseRunner {
public:
    S4Runner() : BaseRunner("S4 一次调频响应") {}

protected:
    std::string DefaultInputCSV()  override { return "../../test_cases/s4_pfr/s4_pfr_test.csv"; }
    std::string DefaultOutputCSV() override { return "../../test_reports/s4_pfr/s4_execution_result.csv"; }

    double ComputePFR(double freq) override {
        // Droop: ΔP = -60.0 * (freq - 50.0) MW
        return -60.0 * (freq - 50.0);
    }

    int ComputeStatusCode(const ResultRow& r, const CsvInputRow& /*row*/) override {
        // S4 场景下频率偏差是预期的, 不设 code=1
        int sc = 0;
        if (r.active_scene != 4.0) sc = 2;
        if (r.tracking_error_pct > 5.0) sc = 3;
        return sc;
    }
};

int main(int argc, char** argv) {
    return S4Runner().Run(argc, argv);
}
