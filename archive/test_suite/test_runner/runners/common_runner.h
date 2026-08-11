
// common_runner.h — 全场景 HIL 测试驱动公共底座


#ifndef COMMON_RUNNER_H
#define COMMON_RUNNER_H

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>
#include <algorithm>
#include <windows.h>

// 使用 <intrin.h> 获取 _mm_pause()
#ifdef _MSC_VER
  #include <intrin.h>
#endif

// ---- 外部 C API: 实时数据库共享内存接口 ----
extern "C" {
    #include "rt_db_api.h"
}

// ============================================================
// 物理仿真参数 (与 AGCSystem/common/config.h 保持一致)
// ============================================================
static const double ESS_RESPONSE_TIME_MS     = 200.0;   // ESS 一阶惯性时间常数
static const double PV_RESPONSE_TIME_MS      = 2000.0;  // PV 逆变器响应时间常数
static const double ESS_MAX_POWER_MW         = 10.0;    // ESS 最大功率
static const double ESS_CAPACITY_MWH         = 20.0;    // ESS 能量容量
static const double PLANT_CAPACITY_MW        = 120.0;   // 真实场站容量 120MW
static const double PV_PER_UNIT_MAX_MW       = 40.0;    // 每台 Runner PV 最大功率 (120MW/3台)
static const double SIM_DT_MS                = 100.0;   // 仿真步长 (ms)
static const int    CONTROL_CYCLE_STEPS      = 10;      // 控制周期间隔 (10 × 100ms = 1s)
static const int    MAX_RETRY                = 3;       // Seqlock 最大重试次数

// ============================================================
// CSV 输入行结构 (14 字段 — 所有场景通用, 列序与生成器严格对齐)
// ============================================================
struct CsvInputRow {
    double timestamp_s;
    double grid_frequency_hz;
    double grid_voltage_pu;
    double rocof_hz_per_s;
    double freq_deviation_hz;
    double pv_total_power_mw;
    double pv_inv1_power_mw;
    double pv_inv2_power_mw;
    double pv_inv3_power_mw;
    double plan_power_mw;
    double irradiance_w_per_m2;
    double ess_soc_pct;
    double ess_power_mw;
    double comm_status;
};

// ============================================================
// 结果输出行结构 (15 字段 — 所有场景通用, v4.35 新增 irradiance)
// ============================================================
struct ResultRow {
    double timestamp_s;
    double grid_frequency_hz;
    double grid_voltage_pu;
    double p_pv_total_mw;
    double p_plan_mw;
    double p_pcc_total_mw;
    double p_ess_target_mw;
    double p_ess_actual_mw;
    double ess_soc_pct;
    double p_pv_target_total_mw;
    double active_scene;
    double p_pfr_mw;
    int    status_code;
    double tracking_error_pct;
    double irradiance_w_per_m2;  // ★ v4.35: 辐照度, 供绘图使用
};

// ============================================================
// SHM Tag 索引缓存 (19 个索引 — 所有场景通用)
// ============================================================
struct ShmTagCache {
    size_t idx_grid_freq       = (size_t)-1;
    size_t idx_grid_voltage    = (size_t)-1;
    size_t idx_grid_rocof      = (size_t)-1;
    size_t idx_pcc_total_power = (size_t)-1;
    size_t idx_pcc_power_setpt = (size_t)-1;
    size_t idx_pv01_power      = (size_t)-1;
    size_t idx_pv02_power      = (size_t)-1;
    size_t idx_pv03_power      = (size_t)-1;
    size_t idx_pv01_target     = (size_t)-1;
    size_t idx_pv02_target     = (size_t)-1;
    size_t idx_pv03_target     = (size_t)-1;
    size_t idx_pv01_irradiance = (size_t)-1;
    size_t idx_ess01_soc       = (size_t)-1;
    size_t idx_ess01_power     = (size_t)-1;
    size_t idx_ess01_target    = (size_t)-1;
    size_t idx_scada_plan      = (size_t)-1;
    size_t idx_scada_comm      = (size_t)-1;
    size_t idx_scada_heartbeat = (size_t)-1;
    size_t idx_agc_scene       = (size_t)-1;
    size_t idx_plant_capacity  = (size_t)-1;
    size_t idx_reset_counter   = (size_t)-1;  // L2: 跨场景锚定
    size_t idx_pv_total_aggregate = (size_t)-1;  // AGC.PV_TOTAL_POWER (HIL 注入优先)
    size_t idx_pv01_power_max     = (size_t)-1;  // PV_01.POWER_MAX (AGC fallback 哨兵)
    size_t idx_pv02_power_max     = (size_t)-1;  // PV_02.POWER_MAX
    size_t idx_pv03_power_max     = (size_t)-1;  // PV_03.POWER_MAX
    size_t idx_s6_strategy        = (size_t)-1;  // S6: AGC.S6_STRATEGY (HIL 自动策略选择)
};

// ============================================================
// 辅助: 基于 tag 字符串查找 SHM 索引
//   rt_db_find_index_by_id(handle, tag) 返回 size_t,
//   (size_t)-1 表示未找到
// ============================================================
static size_t find_index(const rt_db_handle_t* h, const char* tag) {
    size_t idx = rt_db_find_index_by_id(h, tag);
    if (idx == (size_t)-1) {
        fprintf(stderr, "[PHYS-SANDBOX] [WARN] Tag '%s' not found in SHM — will skip\n", tag);
    }
    return idx;
}

// ============================================================
// 辅助: 从 SHM 安全读取 double 值 (Seqlock 退避, 最多 3 次)
// ============================================================
static bool shm_read_double(const rt_db_handle_t* h, size_t idx, double& val) {
    if (idx == (size_t)-1) return false;
    long q = 0;
    struct timespec ts;
    for (int retry = 0; retry < MAX_RETRY; ++retry) {
        if (rt_db_get_value(h, idx, &val, &q, &ts) && q == 1)
            return true;
        #ifdef _MSC_VER
        _mm_pause();
        #endif
    }
    return false;
}

// ============================================================
// 辅助: 安全写入 double 值到 SHM (Seqlock 退避, 最多 3 次)
// ============================================================
static bool shm_write_double(rt_db_handle_t* h, size_t idx, double val) {
    if (idx == (size_t)-1) return false;
    for (int retry = 0; retry < MAX_RETRY; ++retry) {
        if (rt_db_set_value(h, idx, val, 1))
            return true;
        #ifdef _MSC_VER
        _mm_pause();
        #endif
    }
    return false;
}

// ============================================================
// 一阶惯性环节
// ============================================================
static double first_order_lag(double target, double prev, double tau_ms, double dt_ms) {
    if (tau_ms <= 0.0) return target;
    double alpha = 1.0 - exp(-dt_ms / tau_ms);
    return prev + (target - prev) * alpha;
}

// ============================================================
// CSV 解析 — 逐列读取, 字段顺序与生成器输出的 CSV header 严格对齐
// ============================================================
static std::vector<CsvInputRow> parse_csv(const std::string& filepath, int* out_s6_strategy = nullptr) {
    std::vector<CsvInputRow> rows;
    std::ifstream f(filepath);
    if (!f.is_open()) {
        fprintf(stderr, "[PHYS-SANDBOX] [FATAL] Cannot open CSV: %s\n", filepath.c_str());
        return rows;
    }

    std::string line;
    // 解析 CSV 注释行 — 提取 S6 策略号
    while (std::getline(f, line)) {
        if (!line.empty() && line[0] == '#') {
            size_t pos = line.find("S6_STRATEGY=");
            if (pos != std::string::npos && out_s6_strategy) {
                // 跳过 "S6_STRATEGY=" (12 字符: S6_STRATEGY=) 及后续空白字符
                const char* val_start = line.c_str() + pos + 12;
                while (*val_start == ' ' || *val_start == '\t') val_start++;
                char* end = nullptr;
                long parsed = std::strtol(val_start, &end, 10);
                if (end != val_start && parsed >= 1 && parsed <= 3) {
                    *out_s6_strategy = (int)parsed;
                } else {
                    fprintf(stderr, "[PHYS-SANDBOX] [WARN] CSV 注释行策略解析失败: '%s'"
                                    " (期望 # S6_STRATEGY={1|2|3})\n", line.c_str());
                }
            }
            continue;
        }
        break;  // 到达 header 行
    }
    // line 现在是 header, 跳过
    if (line.empty()) return rows;

    while (std::getline(f, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string token;
        CsvInputRow r{};

        auto next = [&]() -> double {
            if (!std::getline(ss, token, ',')) return 0.0;
            return std::strtod(token.c_str(), nullptr);
        };

        // 列序与生成器 fieldnames 严格一致:
        //   timestamp_s, grid_frequency_hz, grid_voltage_pu, rocof_hz_per_s,
        //   freq_deviation_hz, pv_total_power_mw, pv_inv1_power_mw,
        //   pv_inv2_power_mw, pv_inv3_power_mw, plan_power_mw,
        //   irradiance_w_per_m2, ess_soc_pct, ess_power_mw, comm_status
        r.timestamp_s        = next();
        r.grid_frequency_hz  = next();
        r.grid_voltage_pu    = next();
        r.rocof_hz_per_s     = next();
        r.freq_deviation_hz  = next();
        r.pv_total_power_mw  = next();
        r.pv_inv1_power_mw   = next();
        r.pv_inv2_power_mw   = next();
        r.pv_inv3_power_mw   = next();
        r.plan_power_mw      = next();
        r.irradiance_w_per_m2= next();
        r.ess_soc_pct        = next();
        r.ess_power_mw       = next();
        r.comm_status        = next();

        rows.push_back(r);
    }

    printf("[PHYS-SANDBOX] [CSV] 加载 %zu 行测试数据\n", rows.size());
    return rows;
}

// ============================================================
// 写入结果 CSV (14 列标准格式)
// ============================================================
static void write_result_csv(const std::vector<ResultRow>& rows, const std::string& filepath) {
    std::ofstream f(filepath);
    if (!f.is_open()) {
        fprintf(stderr, "[PHYS-SANDBOX] [FATAL] Cannot write result CSV: %s\n", filepath.c_str());
        return;
    }

    f << "timestamp_s,grid_frequency_hz,grid_voltage_pu,p_pv_total_mw,"
      << "p_plan_mw,p_pcc_total_mw,p_ess_target_mw,p_ess_actual_mw,"
      << "ess_soc_pct,p_pv_target_total_mw,active_scene,p_pfr_mw,"
      << "status_code,tracking_error_pct,irradiance_w_per_m2\n";

    for (auto& r : rows) {
        char buf[512];
        snprintf(buf, sizeof(buf),
            "%.3f,%.4f,%.4f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.1f,%.2f,%d,%.4f,%.2f\n",
            r.timestamp_s, r.grid_frequency_hz, r.grid_voltage_pu,
            r.p_pv_total_mw, r.p_plan_mw, r.p_pcc_total_mw,
            r.p_ess_target_mw, r.p_ess_actual_mw, r.ess_soc_pct,
            r.p_pv_target_total_mw, r.active_scene, r.p_pfr_mw,
            r.status_code, r.tracking_error_pct, r.irradiance_w_per_m2);
        f << buf;
    }
    f.close();
    printf("[PHYS-SANDBOX] [RESULT] 写入 %zu 行 → %s\n", rows.size(), filepath.c_str());
}

// ============================================================
// 通用: 全量测点物理复位 — 清除 SHM 中上一轮运行的残留数据
// ============================================================
// 在 HIL 仿真循环开始前调用，确保所有测点在 t=0 注入前处于绝对清洁状态。
// S1~S6 所有测试 Runner 可直接复用此函数。
//
// 参数:
//   handle            — 可变 SHM 句柄 (需要写权限)
//   tag               — Tag 索引缓存
//   pv01_baseline_mw  — PV_01 逆变器基线功率 (MW)，取 CSV 第一行
//   pv02_baseline_mw  — PV_02 逆变器基线功率 (MW)
//   pv03_baseline_mw  — PV_03 逆变器基线功率 (MW)
// ============================================================
static void reset_shm_telemetry_baseline(
    rt_db_handle_t* handle,
    ShmTagCache& tag,
    double pv01_baseline_mw,
    double pv02_baseline_mw,
    double pv03_baseline_mw)
{
    printf("[PHYS-SANDBOX] ═══ 全量测点物理复位 (清除上一轮运行残留数据) ═══\n");

    // 1. PV_01~PV_03: 实际功率 → CSV 第一行基线值, 目标功率 → 0 (MPPT 释放)
    // TARGET_POWER=0 表示 "AGC 未限电, PV 自由 MPPT"
    // 钳位到 PV_PER_UNIT_MAX_MW
    double pv01_clamped = (std::max)(0.0, (std::min)(PV_PER_UNIT_MAX_MW, pv01_baseline_mw));
    double pv02_clamped = (std::max)(0.0, (std::min)(PV_PER_UNIT_MAX_MW, pv02_baseline_mw));
    double pv03_clamped = (std::max)(0.0, (std::min)(PV_PER_UNIT_MAX_MW, pv03_baseline_mw));
    shm_write_double(handle, tag.idx_pv01_power,  pv01_clamped);
    shm_write_double(handle, tag.idx_pv02_power,  pv02_clamped);
    shm_write_double(handle, tag.idx_pv03_power,  pv03_clamped);
    shm_write_double(handle, tag.idx_pv01_target, 0.0);
    shm_write_double(handle, tag.idx_pv02_target, 0.0);
    shm_write_double(handle, tag.idx_pv03_target, 0.0);
    printf("[PHYS-SANDBOX]   PV_01..03 POWER ← %.2f/%.2f/%.2f MW (max=%.0f), TARGET ← 0\n",
           pv01_clamped, pv02_clamped, pv03_clamped, PV_PER_UNIT_MAX_MW);

    // 2. PV_04~PV_64: 实际功率清零 (未使用逆变器)
    for (int pv = 4; pv <= 64; pv++) {
        char tag_buf[64];
        snprintf(tag_buf, sizeof(tag_buf), "PV_%02d.POWER", pv);
        size_t idx = find_index(handle, tag_buf);
        if (idx != (size_t)-1) {
            shm_write_double(handle, idx, 0.0);
        }
    }
    printf("[PHYS-SANDBOX]   PV_04..64 POWER ← 0.0 MW\n");

    // 3. 全部 8 台 ESS: POWER + TARGET → 0.0 (防脏数据污染遥测求和)
    for (int e = 1; e <= 8; e++) {
        char tag_pwr[64], tag_tgt[64];
        snprintf(tag_pwr, sizeof(tag_pwr), "ESS_%02d.POWER", e);
        snprintf(tag_tgt, sizeof(tag_tgt), "ESS_%02d.TARGET_POWER", e);
        size_t idx_pwr = find_index(handle, tag_pwr);
        size_t idx_tgt = find_index(handle, tag_tgt);
        if (idx_pwr != (size_t)-1) shm_write_double(handle, idx_pwr, 0.0);
        if (idx_tgt != (size_t)-1) shm_write_double(handle, idx_tgt, 0.0);
    }
    printf("[PHYS-SANDBOX]   ESS_01..08 POWER/TARGET ← 0.0 MW\n");

    // 4. PCC 并网点总功率 → PV 基线总和 (ESS=0 时 PCC ≡ PV)
    double pcc_baseline = pv01_clamped + pv02_clamped + pv03_clamped;
    shm_write_double(handle, tag.idx_pcc_total_power, pcc_baseline);
    shm_write_double(handle, tag.idx_pcc_power_setpt, pcc_baseline);
    printf("[PHYS-SANDBOX]   PCC.TOTAL_POWER / POWER_SETPOINT ← %.2f MW\n", pcc_baseline);

    // L2: 跨场景锚定 — 递增 RESET_COUNTER 通知 AGC 执行冷启动
    {
        static int reset_sequence = 0;
        reset_sequence++;
        shm_write_double(handle, tag.idx_reset_counter, (double)reset_sequence);
        printf("[PHYS-SANDBOX]   AGC.RESET_COUNTER ← %d (跨场景锚定)\n", reset_sequence);
    }

    printf("[PHYS-SANDBOX] ✅ 全量测点物理复位完成 — SHM 处于 t=0 清洁状态\n");
}

// ============================================================
// 通用: 预缓存全部 19 个 SHM Tag 索引
// ============================================================
static void cache_all_tags(rt_db_handle_t* handle, ShmTagCache& tag) {
    tag.idx_grid_freq       = find_index(handle, "GRID.FREQ");
    tag.idx_grid_voltage    = find_index(handle, "GRID.VOLTAGE");
    tag.idx_grid_rocof      = find_index(handle, "GRID.FREQ_ROCOF");
    tag.idx_pcc_total_power = find_index(handle, "PCC.TOTAL_POWER");
    tag.idx_pcc_power_setpt = find_index(handle, "PCC.POWER_SETPOINT");
    tag.idx_pv01_power      = find_index(handle, "PV_01.POWER");
    tag.idx_pv02_power      = find_index(handle, "PV_02.POWER");
    tag.idx_pv03_power      = find_index(handle, "PV_03.POWER");
    tag.idx_pv01_target     = find_index(handle, "PV_01.TARGET_POWER");
    tag.idx_pv02_target     = find_index(handle, "PV_02.TARGET_POWER");
    tag.idx_pv03_target     = find_index(handle, "PV_03.TARGET_POWER");
    tag.idx_pv01_irradiance = find_index(handle, "PV_01.IRRADIANCE");
    tag.idx_ess01_soc       = find_index(handle, "ESS_01.SOC");
    tag.idx_ess01_power     = find_index(handle, "ESS_01.POWER");
    tag.idx_ess01_target    = find_index(handle, "ESS_01.TARGET_POWER");
    tag.idx_scada_plan      = find_index(handle, "SCADA.PLAN_POWER");
    tag.idx_scada_comm      = find_index(handle, "SCADA.COMM_STATUS");
    tag.idx_scada_heartbeat = find_index(handle, "SCADA.HEARTBEAT");
    tag.idx_agc_scene       = find_index(handle, "AGC.SCENE_ACTIVE");
    tag.idx_plant_capacity  = find_index(handle, "AGC.PLANT_CAPACITY_MW");
    tag.idx_reset_counter   = find_index(handle, "AGC.RESET_COUNTER");
    // ★ v2.0 S5 fix: PV 聚合标签 + POWER_MAX 哨兵
    tag.idx_pv_total_aggregate = find_index(handle, "AGC.PV_TOTAL_POWER");
    tag.idx_pv01_power_max     = find_index(handle, "PV_01.POWER_MAX");
    tag.idx_pv02_power_max     = find_index(handle, "PV_02.POWER_MAX");
    tag.idx_pv03_power_max     = find_index(handle, "PV_03.POWER_MAX");
    tag.idx_s6_strategy        = find_index(handle, "AGC.S6_STRATEGY");
    printf("[PHYS-SANDBOX] ✅ Tag 索引预缓存完成\n");
}

// ============================================================
// BaseRunner — 全场景 HIL Runner 抽象基类 (Template Method)
// ============================================================
// 子类仅需重写场景专属钩子, 调用 Run() 即完成全套 HIL 流程。
// 默认行为 = S1 (稳态基线跟踪)。
// ============================================================
class BaseRunner {
public:
    explicit BaseRunner(const char* label)
        : m_label(label)
        , m_ess_soc_pct(60.0)
        , m_pv1_actual_mw(0.0), m_pv2_actual_mw(0.0), m_pv3_actual_mw(0.0)
        , m_ess_actual_mw(0.0), m_heartbeat_counter(0), m_freeze_heartbeat(false)
        , m_s6_strategy(0) {}

    virtual ~BaseRunner() = default;

    // ---- 模板方法: 完整 HIL 流程 ----
    int Run(int argc, char** argv) {
        SetConsoleOutputCP(65001);
        printf("[PHYS-SANDBOX] ══════════════════════════════════════════\n");
        printf("[PHYS-SANDBOX]   %s HIL 测试驱动程序 v1.0\n", m_label);
        printf("[PHYS-SANDBOX] ══════════════════════════════════════════\n\n");

        // 0. 路径解析
        std::string csv_input  = DefaultInputCSV();
        std::string csv_output = DefaultOutputCSV();
        if (argc >= 2) csv_input  = argv[1];
        if (argc >= 3) csv_output = argv[2];
        printf("[PHYS-SANDBOX] Config: ESS_τ=%.0fms PV_τ=%.0fms PFR_K=scene "
               "ESS_max=%.0fMW PLANT=%.0fMW dt=%.0fms ctrl_cycle=%dsteps\n",
               ESS_RESPONSE_TIME_MS, PV_RESPONSE_TIME_MS,
               ESS_MAX_POWER_MW, PLANT_CAPACITY_MW,
               SIM_DT_MS, CONTROL_CYCLE_STEPS);
        printf("[PHYS-SANDBOX] 输入 CSV:  %s\n", csv_input.c_str());
        printf("[PHYS-SANDBOX] 输出 CSV:  %s\n", csv_output.c_str());

        // 1. 连接共享内存
        printf("\n[PHYS-SANDBOX] 正在连接共享内存...\n");
        if (!rt_db_init(&m_handle, nullptr)) {
            fprintf(stderr, "[PHYS-SANDBOX] [FATAL] 无法连接到共享内存！\n");
            return 1;
        }
        printf("[PHYS-SANDBOX] ✅ 共享内存已连接\n");

        // 2. 预缓存 Tag 索引
        printf("[PHYS-SANDBOX] 正在预缓存 Tag 索引...\n");
        cache_all_tags(&m_handle, m_tags);

        // 3. 加载 CSV (含 S6 策略注释行解析)
        m_input_rows = parse_csv(csv_input, &m_s6_strategy);
        if (m_input_rows.empty()) {
            fprintf(stderr, "[PHYS-SANDBOX] [FATAL] 输入 CSV 为空\n");
            rt_db_cleanup(&m_handle);
            return 1;
        }

        // 4. SHM 全量复位
        double pv01 = m_input_rows[0].pv_inv1_power_mw;
        double pv02 = m_input_rows[0].pv_inv2_power_mw;
        double pv03 = m_input_rows[0].pv_inv3_power_mw;
        reset_shm_telemetry_baseline(&m_handle, m_tags, pv01, pv02, pv03);

        // 5. 初始化物理仿真状态 (v4.30: 钳位到物理上限)
        m_ess_soc_pct   = m_input_rows[0].ess_soc_pct;
        m_ess_actual_mw = 0.0;
        m_pv1_actual_mw = (std::max)(0.0, (std::min)(PV_PER_UNIT_MAX_MW, pv01));
        m_pv2_actual_mw = (std::max)(0.0, (std::min)(PV_PER_UNIT_MAX_MW, pv02));
        m_pv3_actual_mw = (std::max)(0.0, (std::min)(PV_PER_UNIT_MAX_MW, pv03));
        m_heartbeat_counter = 0;

        // 6. HIL 主循环
        printf("\n[PHYS-SANDBOX] 开始 HIL 仿真 (%zu 步 × 100ms = %.0fs)\n",
               m_input_rows.size(), m_input_rows.size() * 0.1);

        shm_write_double(&m_handle, m_tags.idx_scada_comm, 1.0);
        shm_write_double(&m_handle, m_tags.idx_scada_heartbeat, 1.0);
        shm_write_double(&m_handle, m_tags.idx_plant_capacity, PLANT_CAPACITY_MW);
        // ★ v4.31: CSV 注释行指定的 S6 策略 → 写入 SHM (加固版)
        //   检查 tag 索引有效性 + 策略值合法性, 失败时打印 FATAL/WARN 诊断
        if (m_s6_strategy >= 1 && m_s6_strategy <= 3) {
            if (m_tags.idx_s6_strategy == (size_t)-1) {
                fprintf(stderr, "[PHYS-SANDBOX] [FATAL] AGC.S6_STRATEGY tag NOT FOUND in SHM!\n"
                                "[PHYS-SANDBOX]          请确认 rt_db_init.exe 已更新并重新创建共享内存。\n"
                                "[PHYS-SANDBOX]          S6 策略将无法被 AGC 读取, 将回退到 AGC 内部默认值。\n");
            } else if (shm_write_double(&m_handle, m_tags.idx_s6_strategy, (double)m_s6_strategy)) {
                printf("[PHYS-SANDBOX] S6 strategy=%d → AGC.S6_STRATEGY (SHM idx=%zu)\n",
                       m_s6_strategy, m_tags.idx_s6_strategy);
            } else {
                fprintf(stderr, "[PHYS-SANDBOX] [WARN] S6 strategy=%d SHM 写入失败 (seqlock 冲突)\n",
                        m_s6_strategy);
            }
        } else if (m_s6_strategy == 0) {
            fprintf(stderr, "[PHYS-SANDBOX] [WARN] CSV 未指定 S6 策略 (m_s6_strategy=0)\n"
                            "[PHYS-SANDBOX]         若为 S6 测试, 请确保 CSV 第一行为 '# S6_STRATEGY={1|2|3}'\n"
                            "[PHYS-SANDBOX]         若为非 S6 测试, 可忽略此警告。\n");
        }
        printf("[PHYS-SANDBOX] ✅ 初始心跳 + 容量基准已写入 (HEARTBEAT=1, CAPACITY=%.0f MW)\n",
               PLANT_CAPACITY_MW);
        printf("[PHYS-SANDBOX] 等待 AGC 就绪 (3s)...\n");
        std::this_thread::sleep_for(std::chrono::milliseconds(3000));

        auto test_start = std::chrono::steady_clock::now();
        int consecutive_bad = 0;

        // 关键事件跟踪状态
        double prev_freq_for_event = 50.0;
        bool   prev_deadband_state = true;
        double prev_scene_for_event = 0.0;

        for (size_t i = 0; i < m_input_rows.size(); ++i) {
            auto& row = m_input_rows[i];
            double t = row.timestamp_s;
            bool is_ctrl = (i % CONTROL_CYCLE_STEPS == 0);

            // ---- 钩子: 场景专属遥测注入 ----
            int   hb  = m_heartbeat_counter;
            double pv1 = m_pv1_actual_mw;
            double pv2 = m_pv2_actual_mw;
            double pv3 = m_pv3_actual_mw;
            double ess = m_ess_actual_mw;
            OnInjectTelemetry((int)i, row, pv1, pv2, pv3, ess, hb);

            // ---- 写入环境遥测 ----
            shm_write_double(&m_handle, m_tags.idx_grid_freq,    row.grid_frequency_hz);
            shm_write_double(&m_handle, m_tags.idx_grid_voltage, row.grid_voltage_pu);
            shm_write_double(&m_handle, m_tags.idx_grid_rocof,   0.0);
            shm_write_double(&m_handle, m_tags.idx_scada_plan,   row.plan_power_mw);
            shm_write_double(&m_handle, m_tags.idx_pcc_power_setpt, row.plan_power_mw);
            shm_write_double(&m_handle, m_tags.idx_pv01_irradiance, row.irradiance_w_per_m2);
            shm_write_double(&m_handle, m_tags.idx_ess01_soc,    m_ess_soc_pct);
            shm_write_double(&m_handle, m_tags.idx_scada_comm,   row.comm_status);
            shm_write_double(&m_handle, m_tags.idx_pv01_power, pv1);
            shm_write_double(&m_handle, m_tags.idx_pv02_power, pv2);
            shm_write_double(&m_handle, m_tags.idx_pv03_power, pv3);
            shm_write_double(&m_handle, m_tags.idx_ess01_power, ess);
            double pcc_total = pv1 + pv2 + pv3 + ess;
            shm_write_double(&m_handle, m_tags.idx_pcc_total_power, pcc_total);

            // S5 fix: 写入 PV 聚合标签 (AGC read_batch_telemetry 主路径)
            double pv_total = pv1 + pv2 + pv3;
            shm_write_double(&m_handle, m_tags.idx_pv_total_aggregate, pv_total);
            // 写入 POWER_MAX 哨兵 (AGC fallback 路径的逆变器配置过滤器)
            shm_write_double(&m_handle, m_tags.idx_pv01_power_max, PV_PER_UNIT_MAX_MW);
            shm_write_double(&m_handle, m_tags.idx_pv02_power_max, PV_PER_UNIT_MAX_MW);
            shm_write_double(&m_handle, m_tags.idx_pv03_power_max, PV_PER_UNIT_MAX_MW);

            // 清零未模拟的 ESS/PV 标签 — 防脏数据污染控制器遥测求和 ★★★
            // Runner 仅模拟 3 PV + 1 ESS, 其余 SHM 标签须周期性归零
            for (int e = 2; e <= 8; e++) {
                char tag[64];
                snprintf(tag, sizeof(tag), "ESS_%02d.POWER", e);
                size_t idx = find_index(&m_handle, tag);
                if (idx != (size_t)-1) shm_write_double(&m_handle, idx, 0.0);
            }
            for (int p = 4; p <= 64; p++) {
                char tag[64];
                snprintf(tag, sizeof(tag), "PV_%02d.POWER", p);
                size_t idx = find_index(&m_handle, tag);
                if (idx != (size_t)-1) shm_write_double(&m_handle, idx, 0.0);
            }

            // ---- 控制周期边界: 采集 AGC 响应 + 物理闭环 ----
            if (is_ctrl) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));

                double ess_tgt = 0.0, pv1t = 0.0, pv2t = 0.0, pv3t = 0.0;
                shm_read_double(&m_handle, m_tags.idx_ess01_target, ess_tgt);
                shm_read_double(&m_handle, m_tags.idx_pv01_target, pv1t);
                shm_read_double(&m_handle, m_tags.idx_pv02_target, pv2t);
                shm_read_double(&m_handle, m_tags.idx_pv03_target, pv3t);

                double sc = 1.0;
                shm_read_double(&m_handle, m_tags.idx_agc_scene, sc);

                // ---- 钩子: 控制边界扩展 (采集前) ----
                ResultRow pre_info{};
                pre_info.timestamp_s = t;
                pre_info.active_scene = sc;
                pre_info.p_ess_target_mw = ess_tgt;
                pre_info.p_pv_target_total_mw = pv1t + pv2t + pv3t;
                OnControlBoundary((int)m_results.size(), row, pre_info);

                // ---- 钩子: 物理闭环仿真 (更新 m_pv*_actual, m_ess_actual, m_ess_soc) ----
                OnPhysicsStep(ess_tgt, pv1t, pv2t, pv3t, row,
                              m_ess_actual_mw, m_ess_soc_pct,
                              m_pv1_actual_mw, m_pv2_actual_mw, m_pv3_actual_mw);

                //  结果行在物理更新后采集, 直接用成员变量保证 SHM/CSV/LOG 三者一致 
                ResultRow result{};
                result.timestamp_s       = t;
                result.grid_frequency_hz = row.grid_frequency_hz;
                result.grid_voltage_pu   = row.grid_voltage_pu;
                result.p_pv_total_mw     = m_pv1_actual_mw + m_pv2_actual_mw + m_pv3_actual_mw;
                result.p_plan_mw         = row.plan_power_mw;
                result.active_scene      = sc;
                result.p_ess_target_mw    = ess_tgt;
                result.p_pv_target_total_mw = pv1t + pv2t + pv3t;
                result.p_ess_actual_mw   = m_ess_actual_mw;
                result.ess_soc_pct       = m_ess_soc_pct;
                // PCC 直接计算 (不用 SHM 回读, 消除旧值撕裂)
                result.p_pcc_total_mw    = m_pv1_actual_mw + m_pv2_actual_mw
                                         + m_pv3_actual_mw + m_ess_actual_mw;
                //  辐照度直接取当前CSV行, 供评估器绘图
                result.irradiance_w_per_m2 = row.irradiance_w_per_m2;

                // ---- PFR + 状态码 ----
                result.p_pfr_mw = ComputePFR(row.grid_frequency_hz);
                if (row.plan_power_mw > 0.01)
                    result.tracking_error_pct = std::abs(result.p_pcc_total_mw - row.plan_power_mw)
                                              / row.plan_power_mw * 100.0;
                else
                    result.tracking_error_pct = 0.0;
                result.status_code = ComputeStatusCode(result, row);
                m_results.push_back(result);

                // ── 关键事件日志 ──
                {
                    double freq_now = row.grid_frequency_hz;
                    bool deadband_now = (std::abs(freq_now - 50.0) <= 0.05);

                    // 频率穿越死区
                    if (prev_deadband_state && !deadband_now) {
                        printf("[PHYS-SANDBOX] ⚡ freq %.4fHz crossed deadband @ t=%.1fs "
                               "(Δf=%.4fHz > ±0.05Hz)\n",
                               freq_now, t, freq_now - 50.0);
                    }
                    if (!prev_deadband_state && deadband_now) {
                        printf("[PHYS-SANDBOX] ↩ freq %.4fHz returned to deadband @ t=%.1fs "
                               "— ramp-down grace started\n",
                               freq_now, t);
                    }

                    // 场景切换
                    if (std::abs(result.active_scene - prev_scene_for_event) > 0.5
                        && prev_scene_for_event > 0.1) {
                        printf("[PHYS-SANDBOX] 🔄 Scene %.0f→%.0f @ t=%.1fs "
                               "(f=%.4fHz)\n",
                               prev_scene_for_event, result.active_scene,
                               t, freq_now);
                    }

                    prev_deadband_state = deadband_now;
                    prev_scene_for_event = result.active_scene;
                    prev_freq_for_event = freq_now;
                }

                if (m_results.size() % 10 == 0) {
                    printf("[PHYS-SANDBOX] [%3.0fs] scene=%.0f plan=%.1fMW pcc=%.1fMW "
                           "|err|=%.2f%% ess_tgt=%.1fMW soc=%.1f%%\n",
                           t, result.active_scene, result.p_plan_mw, result.p_pcc_total_mw,
                           result.tracking_error_pct, result.p_ess_target_mw, result.ess_soc_pct);
                }
                if (std::abs(result.p_pcc_total_mw) < 0.001 && t > 3.0) consecutive_bad++;
                else consecutive_bad = 0;
            }

            if (i % 5 == 0) {
                if (!m_freeze_heartbeat) m_heartbeat_counter++;
                shm_write_double(&m_handle, m_tags.idx_scada_heartbeat,
                                 (double)m_heartbeat_counter);
            }

            auto elapsed = std::chrono::steady_clock::now() - test_start;
            auto expected = std::chrono::milliseconds((long long)((i + 1) * SIM_DT_MS));
            if (elapsed < expected) std::this_thread::sleep_for(expected - elapsed);
        }

        // 7. 保存结果
        printf("\n[PHYS-SANDBOX] 仿真完成, 共 %zu 个控制周期\n", m_results.size());
        write_result_csv(m_results, csv_output);

        // 8. 摘要
        PrintSummary();
        rt_db_cleanup(&m_handle);
        printf("\n[PHYS-SANDBOX] 已断开共享内存连接\n[PHYS-SANDBOX] [DONE] %s 测试完成\n", m_label);
        return 0;
    }

protected:
    // ================================================================
    // 虚钩子 — 子类按需重写 (默认实现 = S1 行为)
    // ================================================================

    /// 每 100ms 遥测注入前调用。子类可修改 pv/ess/heartbeat 值。
    virtual void OnInjectTelemetry(int /*step*/, CsvInputRow& /*row*/,
                                   double& /*pv1*/, double& /*pv2*/, double& /*pv3*/,
                                   double& /*ess*/, int& /*hb*/) {}

    /// 每个控制周期边界, 采集 AGC 响应后调用。
    virtual void OnControlBoundary(int /*cycle*/, const CsvInputRow& /*row*/,
                                   ResultRow& /*r*/) {}

    /// 默认 PFR = 0 (非 PFR 场景)
    virtual double ComputePFR(double /*freq*/) { return 0.0; }

    /// 默认状态码: scene!=1→2, err>5%→3, |f-50|>0.05→1
    virtual int ComputeStatusCode(const ResultRow& r, const CsvInputRow& row) {
        int sc = 0;
        if (r.active_scene != 1.0) sc = 2;
        if (r.tracking_error_pct > 5.0) sc = 3;
        if (std::abs(row.grid_frequency_hz - 50.0) > 0.05) sc = 1;
        return sc;
    }

    /// 默认物理仿真: PV+ESS 一阶惯性 + SOC 能量积分
    virtual void OnPhysicsStep(double ess_target, double pv1_tgt, double pv2_tgt, double pv3_tgt,
                               const CsvInputRow& row,
                               double& ess_act, double& ess_soc,
                               double& pv1_act, double& pv2_act, double& pv3_act) {
        double dt = SIM_DT_MS * CONTROL_CYCLE_STEPS;
        ess_act = first_order_lag(ess_target, ess_act, ESS_RESPONSE_TIME_MS, dt);
        ess_act = (std::max)(-ESS_MAX_POWER_MW, (std::min)(ESS_MAX_POWER_MW, ess_act));
        double e_mwh = ess_act * (dt / 1000.0 / 3600.0);
        ess_soc -= e_mwh / ESS_CAPACITY_MWH * 100.0;
        ess_soc = (std::max)(0.0, (std::min)(100.0, ess_soc));

        double pv1 = (pv1_tgt > 0.01) ? pv1_tgt : row.pv_inv1_power_mw;
        double pv2 = (pv2_tgt > 0.01) ? pv2_tgt : row.pv_inv2_power_mw;
        double pv3 = (pv3_tgt > 0.01) ? pv3_tgt : row.pv_inv3_power_mw;
        pv1_act = first_order_lag(pv1, pv1_act, PV_RESPONSE_TIME_MS, dt);
        pv2_act = first_order_lag(pv2, pv2_act, PV_RESPONSE_TIME_MS, dt);
        pv3_act = first_order_lag(pv3, pv3_act, PV_RESPONSE_TIME_MS, dt);
        //PV 物理限幅 — 每台 Runner PV 最大 PV_PER_UNIT_MAX_MW (120MW÷3=40MW)
        pv1_act = (std::max)(0.0, (std::min)(PV_PER_UNIT_MAX_MW, pv1_act));
        pv2_act = (std::max)(0.0, (std::min)(PV_PER_UNIT_MAX_MW, pv2_act));
        pv3_act = (std::max)(0.0, (std::min)(PV_PER_UNIT_MAX_MW, pv3_act));
    }

    /// 子类可覆盖以自定义输入/输出 CSV 默认路径
    virtual std::string DefaultInputCSV()  { return "..\\test_cases\\s1_baseline_test.csv"; }
    virtual std::string DefaultOutputCSV() { return "..\\test_reports\\s1_execution_result.csv"; }

    void PrintSummary() {
        if (m_results.empty()) return;
        double mx = 0, sm = 0; int ok = 0, bad = 0;
        for (auto& r : m_results) {
            mx = (std::max)(mx, r.tracking_error_pct);
            sm += r.tracking_error_pct;
            if (r.active_scene == 1.0) ok++; else bad++;
        }
        printf("\n[PHYS-SANDBOX] ══════════════════════════════════════════\n");
        printf("[PHYS-SANDBOX]   测试摘要\n");
        printf("[PHYS-SANDBOX] ══════════════════════════════════════════\n");
        printf("[PHYS-SANDBOX]   控制周期数:      %zu\n", m_results.size());
        printf("[PHYS-SANDBOX]   场景=1 占比:     %d/%zu (%.1f%%)\n", ok, m_results.size(),
               100.0 * ok / m_results.size());
        printf("[PHYS-SANDBOX]   最大跟踪误差:    %.2f%%\n", mx);
        printf("[PHYS-SANDBOX]   平均跟踪误差:    %.2f%%\n", sm / m_results.size());
        printf("[PHYS-SANDBOX] ══════════════════════════════════════════\n");
    }

    // ---- 成员变量 ----
    const char*        m_label;
    rt_db_handle_t     m_handle;
    ShmTagCache        m_tags;
    std::vector<CsvInputRow> m_input_rows;
    std::vector<ResultRow>   m_results;
    double m_ess_soc_pct, m_pv1_actual_mw, m_pv2_actual_mw, m_pv3_actual_mw, m_ess_actual_mw;
    int    m_heartbeat_counter;
    bool   m_freeze_heartbeat = false;  // S6: 通信中断时冻结心跳
    int    m_s6_strategy;              // S6 策略号 (从 CSV 注释行解析)
};

#endif // COMMON_RUNNER_H
