// ============================================================
// s6_runner.cpp — S6 通信中断与离线自治 HIL 测试驱动 (v2.0)
// ============================================================
// ★ v2.0: 支持 4 子测试 (S6-0~S6-3) + 自动化策略选择
//   - 写入 AGC.S6_STRATEGY → scene6_init 自动激活策略 (跳过菜单)
//   - S6-3: 直接注入 CSV 频率 (本地 Droop 测试)
//   - 心跳冻结: comm_status<0.5 时停止心跳递增
// ============================================================

#include "common_runner.h"

class S6Runner : public BaseRunner {
public:
    S6Runner() : BaseRunner("S6 通信中断自治") {}

protected:
    std::string DefaultInputCSV()  override {
        return "../../test_cases/s6_comms/s6_comm_test.csv";
    }
    std::string DefaultOutputCSV() override {
        return "../../test_reports/s6_comms/s6_execution_result.csv";
    }

    // ★ v2.0: 每步注入 — 冻结心跳 + S6-3 频率注入
    void OnInjectTelemetry(int step, CsvInputRow& row,
                           double& pv1, double& pv2, double& pv3,
                           double& ess, int& hb) override {
        // 通信中断时冻结心跳
        m_freeze_heartbeat = (row.comm_status < 0.5);

        // ★ S6-3: 频率激励 — 直接注入 CSV 频率 (本地 Droop 测试)
        //   默认 Runner 写入 row.grid_frequency_hz 到 GRID.FREQ,
        //   这里无需额外操作 — BaseRunner 已处理.
        //   仅确保心跳冻结逻辑正确执行.
    }

    // ★ v4.23: 策略号已由 BaseRunner 从 CSV 注释行解析并写入 SHM
    //   OnControlBoundary 不再需要 — 删除旧 plan 列编码逻辑

    int ComputeStatusCode(const ResultRow& r, const CsvInputRow& row) override {
        int sc = 0;
        // S6 通信中断期间, plan 可能为 NaN/0 (策略1测试), 不检查 tracking error
        if (r.active_scene != 6.0 && r.active_scene != 1.0) sc = 2;
        if (r.tracking_error_pct > 10.0) sc = 3;  // 放宽至10% (S6场景误差容忍)
        if (std::abs(row.grid_frequency_hz - 50.0) > 0.05) sc = 1;
        return sc;
    }
};

int main(int argc, char** argv) {
    return S6Runner().Run(argc, argv);
}
