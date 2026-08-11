/**
 * wind_agc_runner.cpp — 风电AGC HIL Runner
 *
 * 读CSV测试数据 → 逐行注入共享内存 → 驱动AGC响应 → 采集结果
 *
 * 编译 (VS2022 Developer Command Prompt):
 *   cl /utf-8 /std:c++17 /EHsc /O2 ^
 *      /I "../../储能协调控制器实时数据库与缓存模块详细设计/储能协调控制器实时数据库与缓存模块详细设计/include" ^
 *      /I "../../储能协调控制器实时数据库与缓存模块详细设计/储能协调控制器实时数据库与缓存模块详细设计/src/rt_db" ^
 *      runner.cpp ^
 *      "../../储能协调控制器实时数据库与缓存模块详细设计/储能协调控制器实时数据库与缓存模块详细设计/src/rt_db/rt_db_api.c" ^
 *      /Fe:runner.exe /link kernel32.lib
 *
 * 使用:
 *   runner.exe ..\..\output\s1_baseline.csv s1_result.csv
 *   runner.exe ..\..\output\s7_24h_combined.csv s7_result.csv
 *
 * 前提: rt_db_init.exe 必须先启动
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

extern "C" {
#include "rt_db_api.h"
}

// ============================================================
struct CsvRow {
    double timestamp_s;
    double grid_frequency_hz;
    double wind_speed_ms;
    double turbulence;
    double dispatch_target_mw;
    int    dispatch_type;
    double ramp_rate_mw_min;
    double comm_status;
    int    extreme_type;
    double curtail_ratio;
    double schedule_power_mw;
};

struct ResultRow {
    double timestamp_s;
    double actual_power_mw;
    double setpoint_mw;
    double active_scene;
    double freq_hz;
    double error_pct;
};

// ============================================================
static std::vector<CsvRow> load_csv(const char* path) {
    std::vector<CsvRow> rows;
    std::ifstream f(path);
    if (!f.is_open()) { fprintf(stderr, "[FATAL] Cannot open %s\n", path); exit(1); }

    std::string line;
    std::getline(f, line); // skip header
    while (std::getline(f, line)) {
        std::stringstream ss(line);
        std::string tok;
        CsvRow r{};
        auto next = [&]() -> std::string {
            if (ss.peek() == ',') { ss.get(); return ""; }
            std::string v;
            std::getline(ss, v, ',');
            return v;
        };
        r.timestamp_s         = std::stod(next());
        r.grid_frequency_hz   = std::stod(next());
        r.wind_speed_ms       = std::stod(next());
        r.turbulence          = std::stod(next());
        r.dispatch_target_mw  = std::stod(next());
        r.dispatch_type       = std::stoi(next());
        r.ramp_rate_mw_min    = std::stod(next());
        r.comm_status         = std::stod(next());
        r.extreme_type        = std::stoi(next());
        r.curtail_ratio       = std::stod(next());
        r.schedule_power_mw   = std::stod(next());
        rows.push_back(r);
    }
    printf("[LOAD] %zu rows from %s\n", rows.size(), path);
    return rows;
}

// ============================================================
static void write_csv(const char* path, const std::vector<ResultRow>& rows) {
    std::ofstream f(path);
    f << "timestamp_s,actual_power_mw,setpoint_mw,active_scene,freq_hz,error_pct\n";
    for (auto& r : rows) {
        char buf[256];
        snprintf(buf, sizeof(buf), "%.3f,%.4f,%.4f,%.1f,%.4f,%.4f\n",
                 r.timestamp_s, r.actual_power_mw, r.setpoint_mw,
                 r.active_scene, r.freq_hz, r.error_pct);
        f << buf;
    }
    printf("[SAVE] %zu rows -> %s\n", rows.size(), path);
}

// ============================================================
static size_t idx_total_power   = (size_t)-1;
static size_t idx_setpoint      = (size_t)-1;
static size_t idx_mode          = (size_t)-1;
static size_t idx_wind_speed    = (size_t)-1;
static size_t idx_fluctuation   = (size_t)-1;
static size_t idx_freq          = (size_t)-1;
static size_t idx_schedule      = (size_t)-1;
static size_t idx_comm_healthy  = (size_t)-1;
static size_t idx_extreme_type  = (size_t)-1;
static size_t idx_curtail_ratio = (size_t)-1;

static void lookup_indices(rt_db_handle_t* db) {
    idx_total_power   = rt_db_find_index_by_id(db, "WIND_AGC.TotalPower");
    idx_setpoint      = rt_db_find_index_by_id(db, "WIND_AGC.Setpoint");
    idx_mode          = rt_db_find_index_by_id(db, "WIND_AGC.Mode");
    idx_wind_speed    = rt_db_find_index_by_id(db, "WIND_AGC.WindSpeed");
    idx_fluctuation   = rt_db_find_index_by_id(db, "WIND_AGC.FluctuationWarning");
    idx_freq          = rt_db_find_index_by_id(db, "GRID.Frequency");
    idx_schedule      = rt_db_find_index_by_id(db, "WIND_AGC.SchedulePower");
    idx_comm_healthy  = rt_db_find_index_by_id(db, "COMM.IsHealthy");
    idx_extreme_type  = rt_db_find_index_by_id(db, "EXTREME.SubType");
    idx_curtail_ratio = rt_db_find_index_by_id(db, "CURTAIL.Ratio");
    printf("[SHM] Data point indices resolved\n");
}

// ============================================================
int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: runner.exe <input_csv> <output_csv>\n");
        fprintf(stderr, "  e.g. runner.exe output/s1_baseline.csv s1_result.csv\n");
        return 1;
    }
    const char* input_csv  = argv[1];
    const char* output_csv = argv[2];

#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    printf("=== Wind AGC HIL Runner ===\n");
    printf("  Input:  %s\n", input_csv);
    printf("  Output: %s\n", output_csv);

    // 1. Load CSV
    auto inputs = load_csv(input_csv);
    if (inputs.empty()) { fprintf(stderr, "[FATAL] Empty CSV\n"); return 1; }

    // 2. Connect to shared memory
    rt_db_handle_t db{};
    if (!rt_db_init(&db, nullptr)) {
        fprintf(stderr, "[FATAL] RT_DB not found. Start rt_db_init.exe first!\n");
        return 1;
    }
    lookup_indices(&db);

    // 3. Run injection loop
    std::vector<ResultRow> results;
    results.reserve(inputs.size());

    double prev_target = -1e9;
    int prev_dtype = -1;

    auto t_start = std::chrono::steady_clock::now();

    for (size_t i = 0; i < inputs.size(); i++) {
        auto& in = inputs[i];

        // --- Inject environment signals ---
        if (idx_freq          != (size_t)-1) rt_db_set_value(&db, idx_freq,          in.grid_frequency_hz, 1);
        if (idx_wind_speed    != (size_t)-1) rt_db_set_value(&db, idx_wind_speed,    in.wind_speed_ms,     1);
        if (idx_fluctuation   != (size_t)-1) rt_db_set_value(&db, idx_fluctuation,   in.turbulence > 0.25 ? 1.0 : 0.0, 1);
        if (idx_comm_healthy  != (size_t)-1) rt_db_set_value(&db, idx_comm_healthy,  in.comm_status,        1);
        if (idx_extreme_type  != (size_t)-1) rt_db_set_value(&db, idx_extreme_type,  (double)in.extreme_type, 1);
        if (idx_curtail_ratio != (size_t)-1) rt_db_set_value(&db, idx_curtail_ratio, in.curtail_ratio,       1);

        // Inject dispatch command (only when target changes)
        if (in.dispatch_target_mw != prev_target || in.dispatch_type != prev_dtype) {
            if (idx_schedule != (size_t)-1) rt_db_set_value(&db, idx_schedule, in.dispatch_target_mw, 1);
            if (idx_setpoint != (size_t)-1) rt_db_set_value(&db, idx_setpoint,  in.dispatch_target_mw, 1);

            if (in.dispatch_type > 0) {
                ControlCommand cmd{};
                snprintf(cmd.device_id, sizeof(cmd.device_id), "WIND_AGC");
                cmd.command_type = in.dispatch_type;
                cmd.value = in.dispatch_target_mw;
                cmd.priority = 1;
                rt_db_push_command(&db, &cmd);
            }
            prev_target = in.dispatch_target_mw;
            prev_dtype = in.dispatch_type;
        }

        // --- Simulate real time pacing ---
        if (i > 0) {
            double dt = in.timestamp_s - inputs[i-1].timestamp_s;
            if (dt > 0) {
                auto elapsed = std::chrono::steady_clock::now() - t_start;
                double sim_elapsed = in.timestamp_s;
                double real_elapsed = std::chrono::duration<double>(elapsed).count();
                if (sim_elapsed > real_elapsed) {
                    int sleep_ms = (int)((sim_elapsed - real_elapsed) * 1000.0 * 0.1); // 10x speed
                    if (sleep_ms > 0 && sleep_ms < 500)
                        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
                }
            }
        }

        // --- Collect AGC response ---
        ResultRow out{};
        out.timestamp_s = in.timestamp_s;

        double val;
        if (idx_total_power != (size_t)-1 && rt_db_get_value(&db, idx_total_power, &val, nullptr, nullptr))
            out.actual_power_mw = val;
        if (idx_setpoint    != (size_t)-1 && rt_db_get_value(&db, idx_setpoint, &val, nullptr, nullptr))
            out.setpoint_mw = val;
        if (idx_mode        != (size_t)-1 && rt_db_get_value(&db, idx_mode, &val, nullptr, nullptr))
            out.active_scene = val;
        if (idx_freq        != (size_t)-1 && rt_db_get_value(&db, idx_freq, &val, nullptr, nullptr))
            out.freq_hz = val;
        if (in.dispatch_target_mw > 0.01)
            out.error_pct = std::abs(out.actual_power_mw - in.dispatch_target_mw) / in.dispatch_target_mw * 100.0;

        results.push_back(out);

        // Progress
        if (i % 100 == 0 && i > 0) {
            printf("\r  Progress: %zu/%zu (%.1f%%)  Scene=%.0f  Pwr=%.2f MW",
                   i, inputs.size(), 100.0*i/inputs.size(),
                   out.active_scene, out.actual_power_mw);
            fflush(stdout);
        }
    }

    printf("\r  Progress: %zu/%zu (100%%)                         \n", inputs.size(), inputs.size());

    // 4. Save results
    write_csv(output_csv, results);

    // 5. Quick summary
    int scene_changes = 0;
    double last_scene = -1;
    for (auto& r : results) {
        if (r.active_scene != last_scene) { scene_changes++; last_scene = r.active_scene; }
    }
    double avg_error = 0;
    for (auto& r : results) avg_error += r.error_pct;
    avg_error /= results.size();

    printf("\n=== Summary ===\n");
    printf("  Scene changes: %d\n", scene_changes);
    printf("  Avg error:     %.2f %%\n", avg_error);
    printf("  Max error:     ");
    double max_err = 0;
    for (auto& r : results) if (r.error_pct > max_err) max_err = r.error_pct;
    printf("%.2f %%\n", max_err);

    rt_db_cleanup(&db);
    return 0;
}