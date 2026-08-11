// ============================================================
// s3_runner.cpp — S3 调度指令阶跃 HIL 测试驱动
// ============================================================
// 阶跃场景: 计划功率发生大阶跃, 验证 AGC 对阶跃指令的跟踪性能。
// 场景断言: 阶跃期间 active_scene 应为 3。
// ============================================================

#include "common_runner.h"

class S3Runner : public BaseRunner {
public:
    S3Runner() : BaseRunner("S3 计划值阶跃") {}

protected:
    std::string DefaultInputCSV()  override { return "../../test_cases/s3_step/s3_step_test.csv"; }
    std::string DefaultOutputCSV() override { return "../../test_reports/s3_step/s3_execution_result.csv"; }

    // ★ S3 不重写 OnInjectTelemetry — 依赖默认物理模型
    //   S3 的 AGC 主动写 PV TARGET 实现限电/增发,
    //   物理模型 (OnPhysicsStep) 正确跟踪 AGC target
    //   Reset 已修复: PV target → 0 (MPPT), 不再锁死

    int ComputeStatusCode(const ResultRow& r, const CsvInputRow& row) override {
        int sc = 0;
        if (r.active_scene != 3.0) sc = 2;
        if (r.tracking_error_pct > 5.0) sc = 3;
        if (std::abs(row.grid_frequency_hz - 50.0) > 0.05) sc = 1;
        return sc;
    }
};

int main(int argc, char** argv) {
    return S3Runner().Run(argc, argv);
}
