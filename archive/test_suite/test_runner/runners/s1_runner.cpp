// ============================================================
// s1_runner.cpp — 场景1 (S1) 稳态基线跟踪 HIL 测试驱动程序
// ============================================================
// 版本: v2.0  |  日期: 2026-07-24
// ============================================================
// 变更记录:
//   v2.0 — Phase 0.5 OOP 重构: 继承 BaseRunner, S1 使用全默认行为
//   v1.4 — Phase 0 重构: 抽取公共底座到 common_runner.h
//   v1.3 — HIL 启动物理数据残留根除 (reset_shm_telemetry_baseline)
// ============================================================
// 编译 (MSVC Developer Command Prompt, 从 test_runner 目录):
//   cl /EHsc /std:c++17 /O2 /utf-8 /I ..\..\src\rt_db_adapter
//      s1_runner.cpp ..\..\src\rt_db_adapter\rt_db_api.c
//      /Fe:s1_runner.exe /link kernel32.lib
// ============================================================

#include "common_runner.h"

// S1 使用 BaseRunner 的 100% 默认行为, 无需任何重写
class S1Runner : public BaseRunner {
public:
    S1Runner() : BaseRunner("S1 稳态基线跟踪") {}

protected:
    std::string DefaultInputCSV()  override { return "../../test_cases/s1_baseline/s1_baseline_test.csv"; }
    std::string DefaultOutputCSV() override { return "../../test_reports/s1_baseline/s1_execution_result.csv"; }
};

int main(int argc, char** argv) {
    return S1Runner().Run(argc, argv);
}
