#include "../rt_db/rt_db_structs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#include <conio.h>
#define sleep(x) Sleep((x) * 1000)
static HANDLE g_shared_memory_handle = NULL;
static const char* SHARED_MEMORY_NAME = "RT_DB_SHARED_MEMORY";
static volatile bool g_shutdown_requested = false;
#else
#include <sys/shm.h>
#include <sys/ipc.h>
#include <unistd.h>
static volatile sig_atomic_t g_shutdown_requested = 0;
#endif

// 信号处理函数
#ifndef _WIN32
void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        g_shutdown_requested = 1;
        printf("\nReceived shutdown signal, cleaning up...\n");
    }
}
#else
BOOL WINAPI console_handler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_CLOSE_EVENT) {
        g_shutdown_requested = true;
        printf("\nReceived shutdown signal, cleaning up...\n");
        return TRUE;
    }
    return FALSE;
}
#endif

// Windows平台的共享内存操作
#ifdef _WIN32
int create_shared_memory_segment(void** shm_addr) {
    g_shared_memory_handle = CreateFileMapping(
        INVALID_HANDLE_VALUE,
        NULL,
        PAGE_READWRITE,
        0,
        sizeof(SharedMemorySegment),
        SHARED_MEMORY_NAME
    );
    
    if (g_shared_memory_handle == NULL) {
        fprintf(stderr, "CreateFileMapping failed: %lu\n", GetLastError());
        return -1;
    }
    
    // 检查是否已存在
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        fprintf(stderr, "Shared memory already exists! Please stop existing instances first.\n");
        CloseHandle(g_shared_memory_handle);
        return -2;
    }
    
    *shm_addr = MapViewOfFile(
        g_shared_memory_handle,
        FILE_MAP_ALL_ACCESS,
        0,
        0,
        sizeof(SharedMemorySegment)
    );
    
    if (*shm_addr == NULL) {
        fprintf(stderr, "MapViewOfFile failed: %lu\n", GetLastError());
        CloseHandle(g_shared_memory_handle);
        return -1;
    }
    
    return 0;
}

void cleanup_shared_memory(void* shm_addr) {
    if (shm_addr != NULL) {
        UnmapViewOfFile(shm_addr);
    }
    if (g_shared_memory_handle != NULL) {
        CloseHandle(g_shared_memory_handle);
        g_shared_memory_handle = NULL;
    }
}
#endif

// 初始化共享内存段
void initialize_shared_memory_segment(SharedMemorySegment* segment) {
    printf("Initializing shared memory segment...\n");
    
    // 清零整个内存段
    memset(segment, 0, sizeof(SharedMemorySegment));
    
    // 初始化头部信息
    atomic_init(&segment->header.write_count, 0);
    atomic_init(&segment->header.shutdown_requested, false);
    atomic_init(&segment->header.connected_clients, 0);
    
#ifdef _WIN32
    segment->header.manager_pid = _getpid();
#else
    segment->header.manager_pid = getpid();
#endif
    segment->header.num_data_points = MAX_DATA_POINTS;
    
    // 获取当前时间
#ifdef _WIN32
    FILETIME ft;
    ULARGE_INTEGER ui;
    GetSystemTimeAsFileTime(&ft);
    ui.LowPart = ft.dwLowDateTime;
    ui.HighPart = ft.dwHighDateTime;
    ui.QuadPart = ui.QuadPart - 116444736000000000ULL;
    segment->header.last_updated.tv_sec = (long)(ui.QuadPart / 10000000ULL);
    segment->header.last_updated.tv_nsec = (long)((ui.QuadPart % 10000000ULL) * 100);
#else
    clock_gettime(CLOCK_REALTIME, &segment->header.last_updated);
#endif
    
    // 初始化数据点
    printf("Initializing %d data points...\n", MAX_DATA_POINTS);
    for (size_t i = 0; i < MAX_DATA_POINTS; i++) {
        snprintf(segment->data_points[i].point_id, MAX_POINT_ID_LEN, "UNUSED_%zu", i);
        atomic_init(&segment->data_points[i].value, 0.0);
        atomic_init(&segment->data_points[i].quality, QUALITY_BAD);
        atomic_init(&segment->data_points[i].sequence, 0);
        strcpy(segment->data_points[i].units, "");
        segment->data_points[i].timestamp = segment->header.last_updated;
    }

    // ================================================================
    // 注册预定义数据点（覆盖部分 UNUSED_ 槽位）
    // 布局：0-7 储能BMS/PCS, 8-499 新能源AGC, 500+ 剩余
    // ================================================================
    size_t slot = 0;

    // --- 储能 BMS/PCS (0-7) ---
    const char* battery_points[] = {
        "BMS_01.SOC", "BMS_01.Voltage", "BMS_01.Current",
        "PCS_01.Power", "PCS_01.Status", "PCS_01.Frequency",
        "SCADA.SystemStatus", "SCADA.TotalPower"
    };
    const char* battery_units[] = {"%", "V", "A", "kW", "", "Hz", "", "kW"};
    for (int i = 0; i < 8; i++) {
        strncpy(segment->data_points[slot].point_id, battery_points[i], MAX_POINT_ID_LEN - 1);
        strncpy(segment->data_points[slot].units,      battery_units[i],   MAX_UNIT_LEN - 1);
        atomic_init(&segment->data_points[slot].quality, QUALITY_GOOD);
        slot++;
    }

    // --- 风电AGC全场级数据点 (8-18) ---
    const char* wind_agc_points[] = {
        "WIND_AGC.TotalPower", "WIND_AGC.SchedulePower", "WIND_AGC.Setpoint",
        "WIND_AGC.ErrorPU",    "WIND_AGC.FeedforwardMW", "WIND_AGC.FeedbackMW",
        "WIND_AGC.MAE",        "WIND_AGC.RMSE",           "WIND_AGC.QualifiedRate",
        "WIND_AGC.MaxError",   "WIND_AGC.CycleCount"
    };
    const char* wind_agc_units[] = {
        "MW", "MW", "MW", "%", "MW", "MW", "%", "%", "%", "%", ""
    };
    for (int i = 0; i < 11; i++) {
        strncpy(segment->data_points[slot].point_id, wind_agc_points[i], MAX_POINT_ID_LEN - 1);
        strncpy(segment->data_points[slot].units,    wind_agc_units[i],  MAX_UNIT_LEN - 1);
        atomic_init(&segment->data_points[slot].quality, QUALITY_GOOD);
        slot++;
    }

    // --- 风机级数据点 TURBINE_000~TURBINE_099 (19-318) ---
    // 每台风机3个点: Power, WindSpeed, Command
    for (int t = 0; t < 100; t++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "TURBINE_%03d.Power", t);
        strncpy(segment->data_points[slot].point_id, buf, MAX_POINT_ID_LEN - 1);
        strncpy(segment->data_points[slot].units, "MW", MAX_UNIT_LEN - 1);
        atomic_init(&segment->data_points[slot].quality, QUALITY_GOOD);
        slot++;

        snprintf(buf, sizeof(buf), "TURBINE_%03d.WindSpeed", t);
        strncpy(segment->data_points[slot].point_id, buf, MAX_POINT_ID_LEN - 1);
        strncpy(segment->data_points[slot].units, "m/s", MAX_UNIT_LEN - 1);
        atomic_init(&segment->data_points[slot].quality, QUALITY_GOOD);
        slot++;

        snprintf(buf, sizeof(buf), "TURBINE_%03d.Command", t);
        strncpy(segment->data_points[slot].point_id, buf, MAX_POINT_ID_LEN - 1);
        strncpy(segment->data_points[slot].units, "MW", MAX_UNIT_LEN - 1);
        atomic_init(&segment->data_points[slot].quality, QUALITY_GOOD);
        slot++;
    }

    // --- 风电AGC扩展数据点 (WindSpeed扰动场景) ---
    // 新增: Mode, WindSpeed, FluctuationWarning, AvgPitch, AvgSpeed
    {
        const char* ext_points[] = {
            "WIND_AGC.Mode", "WIND_AGC.WindSpeed",
            "WIND_AGC.FluctuationWarning",
            "WIND_AGC.AvgPitch", "WIND_AGC.AvgSpeed"
        };
        const char* ext_units[] = {"", "m/s", "", "deg", "RPM"};
        for (int i = 0; i < 5; i++) {
            strncpy(segment->data_points[slot].point_id, ext_points[i], MAX_POINT_ID_LEN - 1);
            strncpy(segment->data_points[slot].units,    ext_units[i],  MAX_UNIT_LEN - 1);
            atomic_init(&segment->data_points[slot].quality, QUALITY_GOOD);
            slot++;
        }
    }

    // --- 风机扩展数据点: PitchAngle, RotorSpeed, UpMargin, DownMargin ---
    for (int t = 0; t < 100; t++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "TURBINE_%03d.PitchAngle", t);
        strncpy(segment->data_points[slot].point_id, buf, MAX_POINT_ID_LEN - 1);
        strncpy(segment->data_points[slot].units, "deg", MAX_UNIT_LEN - 1);
        atomic_init(&segment->data_points[slot].quality, QUALITY_GOOD);
        slot++;

        snprintf(buf, sizeof(buf), "TURBINE_%03d.RotorSpeed", t);
        strncpy(segment->data_points[slot].point_id, buf, MAX_POINT_ID_LEN - 1);
        strncpy(segment->data_points[slot].units, "RPM", MAX_UNIT_LEN - 1);
        atomic_init(&segment->data_points[slot].quality, QUALITY_GOOD);
        slot++;

        snprintf(buf, sizeof(buf), "TURBINE_%03d.UpMargin", t);
        strncpy(segment->data_points[slot].point_id, buf, MAX_POINT_ID_LEN - 1);
        strncpy(segment->data_points[slot].units, "MW", MAX_UNIT_LEN - 1);
        atomic_init(&segment->data_points[slot].quality, QUALITY_GOOD);
        slot++;

        snprintf(buf, sizeof(buf), "TURBINE_%03d.DownMargin", t);
        strncpy(segment->data_points[slot].point_id, buf, MAX_POINT_ID_LEN - 1);
        strncpy(segment->data_points[slot].units, "MW", MAX_UNIT_LEN - 1);
        atomic_init(&segment->data_points[slot].quality, QUALITY_GOOD);
        slot++;
    }

    printf("Pre-registered %zu named data points (BMS/PCS + WIND_AGC + Turbines x7 fields)\n", slot);

    // --- 场景3 一次调频: GRID.* + FR_CTRL.* ---
    {
        const char* fr_points[] = {
            "GRID.Frequency", "GRID.FrequencyDelta",
            "FR_CTRL.State", "FR_CTRL.TotalFRPower",
            "FR_CTRL.DeltaF", "FR_CTRL.Proportional", "FR_CTRL.Integral"
        };
        const char* fr_units[] = {
            "Hz", "Hz", "", "kW", "Hz", "kW", "kW"
        };
        for (int i = 0; i < 7; i++) {
            strncpy(segment->data_points[slot].point_id, fr_points[i], MAX_POINT_ID_LEN - 1);
            strncpy(segment->data_points[slot].units,    fr_units[i],  MAX_UNIT_LEN - 1);
            atomic_init(&segment->data_points[slot].quality, QUALITY_GOOD);
            slot++;
        }
    }

    // --- 风机调频功率: TURBINE_000.FR_Power ~ TURBINE_099.FR_Power ---
    for (int t = 0; t < 100; t++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "TURBINE_%03d.FR_Power", t);
        strncpy(segment->data_points[slot].point_id, buf, MAX_POINT_ID_LEN - 1);
        strncpy(segment->data_points[slot].units, "kW", MAX_UNIT_LEN - 1);
        atomic_init(&segment->data_points[slot].quality, QUALITY_GOOD);
        slot++;
    }

    // --- 场景5 光电AGC限电备用: PV_AGC.* ---
    {
        const char* pv_points[] = {
            "PV_AGC.TotalPower", "PV_AGC.TargetPower", "PV_AGC.PV_Avail",
            "PV_AGC.PV_Command", "PV_AGC.ESS_Command", "PV_AGC.SOC",
            "PV_AGC.Mode", "PV_AGC.FeedforwardESS", "PV_AGC.FeedbackOutput",
            "PV_AGC.Deviation"
        };
        const char* pv_units[] = {
            "MW", "MW", "MW", "MW", "MW", "%", "", "MW", "MW", "MW"
        };
        for (int i = 0; i < 10; i++) {
            strncpy(segment->data_points[slot].point_id, pv_points[i], MAX_POINT_ID_LEN - 1);
            strncpy(segment->data_points[slot].units,    pv_units[i],  MAX_UNIT_LEN - 1);
            atomic_init(&segment->data_points[slot].quality, QUALITY_GOOD);
            slot++;
        }
    }

    // --- 场景5 限电管理: CURTAIL.* ---
    {
        const char* c_points[] = {
            "CURTAIL.Ratio", "CURTAIL.Mode", "CURTAIL.TotalAvailable",
            "CURTAIL.TotalMinTech", "CURTAIL.EfficientPower",
            "CURTAIL.ReserveUp", "CURTAIL.ReserveDown",
            "CURTAIL.R_thresh", "CURTAIL.T_thresh_hours",
            "REST_ROTATE.HotStandbyCount", "REST_ROTATE.ActiveCount",
            "REST_ROTATE.NextStartId", "REST_ROTATE.NextStopId"
        };
        const char* c_units[] = {
            "", "", "MW", "MW", "MW", "MW", "MW", "", "h", "", "", "", ""
        };
        for (int i = 0; i < 13; i++) {
            strncpy(segment->data_points[slot].point_id, c_points[i], MAX_POINT_ID_LEN - 1);
            strncpy(segment->data_points[slot].units,    c_units[i],  MAX_UNIT_LEN - 1);
            atomic_init(&segment->data_points[slot].quality, QUALITY_GOOD);
            slot++;
        }
    }

    // --- 场景6 安全模式管理: SAFETY.* + COMM.* + EXTREME.* ---
    {
        const char* s_points[] = {
            "SAFETY.CurrentMode", "SAFETY.FrozenPowerMW",
            "SAFETY.RecoveryTargetMW", "SAFETY.RecoveryRampRate",
            "SAFETY.RecoveryActive",
            "COMM.IsHealthy", "COMM.LastHeartbeatSec",
            "EXTREME.SubType", "EXTREME.AvgWindSpeed", "EXTREME.AvgTurbulence",
            "EXTREME.Triggered"
        };
        const char* s_units[] = {
            "", "MW", "MW", "MW/min", "", "", "s", "", "m/s", "", ""
        };
        for (int i = 0; i < 11; i++) {
            strncpy(segment->data_points[slot].point_id, s_points[i], MAX_POINT_ID_LEN - 1);
            strncpy(segment->data_points[slot].units,    s_units[i],  MAX_UNIT_LEN - 1);
            atomic_init(&segment->data_points[slot].quality, QUALITY_GOOD);
            slot++;
        }
    }

    // --- 场景6 AVC电压控制: AVC.* ---
    {
        const char* a_points[] = {
            "AVC.StationVoltage", "AVC.TargetVoltage", "AVC.ReactivePower",
            "AVC.Deviation"
        };
        const char* a_units[] = { "pu", "pu", "Mvar", "%" };
        for (int i = 0; i < 4; i++) {
            strncpy(segment->data_points[slot].point_id, a_points[i], MAX_POINT_ID_LEN - 1);
            strncpy(segment->data_points[slot].units,    a_units[i],  MAX_UNIT_LEN - 1);
            atomic_init(&segment->data_points[slot].quality, QUALITY_GOOD);
            slot++;
        }
    }

    // --- 场景6 24h调度: SCHEDULE.* ---
    {
        const char* sch_points[] = {
            "SCHEDULE.HourOfDay", "SCHEDULE.CurtailRatio", "SCHEDULE.CurtailMode",
            "SCHEDULE.WindFactor", "SCHEDULE.IsReserveHour"
        };
        const char* sch_units[] = { "h", "", "", "", "" };
        for (int i = 0; i < 5; i++) {
            strncpy(segment->data_points[slot].point_id, sch_points[i], MAX_POINT_ID_LEN - 1);
            strncpy(segment->data_points[slot].units,    sch_units[i],  MAX_UNIT_LEN - 1);
            atomic_init(&segment->data_points[slot].quality, QUALITY_GOOD);
            slot++;
        }
    }

    // --- 场景4 爬坡跟踪扩展: RAMP.* ---
    {
        const char* r_points[] = {
            "RAMP.CurrentTarget", "RAMP.DesiredTarget", "RAMP.CompensatedTarget",
            "RAMP.IsActive", "RAMP.RampRateMWmin", "RAMP.ElapsedSec",
            "RAMP.ProtectionSafe"
        };
        const char* r_units[] = { "MW", "MW", "MW", "", "MW/min", "s", "" };
        for (int i = 0; i < 7; i++) {
            strncpy(segment->data_points[slot].point_id, r_points[i], MAX_POINT_ID_LEN - 1);
            strncpy(segment->data_points[slot].units,    r_units[i],  MAX_UNIT_LEN - 1);
            atomic_init(&segment->data_points[slot].quality, QUALITY_GOOD);
            slot++;
        }
    }

    printf("Pre-registered %zu total named data points (all scenarios)\n", slot);
    
    // 初始化指令队列
    atomic_init(&segment->command_queue.head, 0);
    atomic_init(&segment->command_queue.tail, 0);
    
    printf("Shared memory initialization completed successfully.\n");
}

// 监控共享内存状态
// Count named (non-UNUSED) data points
static size_t count_named_points(SharedMemorySegment* seg) {
    size_t n = 0;
    for (size_t i = 0; i < MAX_DATA_POINTS; i++) {
        if (strncmp(seg->data_points[i].point_id, "UNUSED_", 7) != 0) n++;
        else break;
    }
    return n;
}

// Helper: find a data point by ID and return its value
static double get_point(SharedMemorySegment* seg, const char* id) {
    for (size_t i = 0; i < MAX_DATA_POINTS; i++) {
        if (strncmp(seg->data_points[i].point_id, "UNUSED_", 7) == 0) break;
        if (strcmp(seg->data_points[i].point_id, id) == 0) {
            return seg->data_points[i].value;
        }
    }
    return 0.0;
}

// Scene name mapping
static const char* scene_name(int s) {
    switch(s) {
        case 1: return "Normal AGC";
        case 2: return "Wind Suppress";
        case 3: return "Freq Regulate";
        case 4: return "Ramp Track";
        case 5: return "Curtailment";
        case 6: return "Safety Mode";
        default: return "--";
    }
}

void monitor_shared_memory(SharedMemorySegment* seg) {
    size_t last_writes = 0;
    int interval = 2;
    size_t npts = count_named_points(seg);

    printf("\n");
    printf("  RT_DB Live Monitor | %zu pts | refresh %ds | Ctrl+C exit\n\n", npts, interval);

    while (!g_shutdown_requested &&
           !atomic_load_explicit(&seg->header.shutdown_requested, memory_order_relaxed)) {

        size_t writes = atomic_load_explicit(&seg->header.write_count, memory_order_relaxed);
        int clients   = atomic_load_explicit(&seg->header.connected_clients, memory_order_relaxed);
        double wps    = (writes - last_writes) / (double)interval;

        // Read live data points
        double totalPwr  = get_point(seg, "WIND_AGC.TotalPower");
        double setpoint  = get_point(seg, "WIND_AGC.Setpoint");
        double schedule  = get_point(seg, "WIND_AGC.SchedulePower");
        double errorPU   = get_point(seg, "WIND_AGC.ErrorPU");
        int    scene     = (int)get_point(seg, "WIND_AGC.Mode");
        double windSpeed = get_point(seg, "WIND_AGC.WindSpeed");
        double fluctWarn = get_point(seg, "WIND_AGC.FluctuationWarning");

        double freqHz    = get_point(seg, "GRID.Frequency");

        int    safeMode  = (int)get_point(seg, "SAFETY.CurrentMode");
        double frozenMW  = get_point(seg, "SAFETY.FrozenPowerMW");
        int    commOk    = (int)get_point(seg, "COMM.IsHealthy");
        int    extType   = (int)get_point(seg, "EXTREME.SubType");

        double curtRatio = get_point(seg, "CURTAIL.Ratio");
        double resvUp    = get_point(seg, "CURTAIL.ReserveUp");
        double resvDn    = get_point(seg, "CURTAIL.ReserveDown");

        // Clear previous display (16 lines of content)
        printf("\033[17A\033[0J");

        printf("  Clients: %d | Writes: %zu | %.0f w/s | Active: Scene %d (%s)\n\n",
               clients, writes, wps, scene, scene_name(scene));

        printf("  ---- Wind Farm AGC ----\n");
        printf("  TotalPower: %8.1f MW   Setpoint:  %8.1f MW\n", totalPwr, setpoint);
        printf("  Schedule:   %8.1f MW   Error:     %8.2f %%\n", schedule, errorPU);
        printf("  WindSpeed:  %8.1f m/s  FluctWarn: %8.1f\n", windSpeed, fluctWarn);

        printf("  ---- Grid ----\n");
        printf("  Frequency:  %8.2f Hz\n", freqHz);

        printf("  ---- Safety ----\n");
        printf("  Mode: %d(%s)  CommOK: %d  Extreme: %d",
               safeMode, safeMode==0?"NORMAL":safeMode==1?"FREEZE":"AUTO", commOk, extType);
        if (frozenMW > 0.01) printf("  FrozenPwr: %.1f MW", frozenMW);
        printf("\n");

        printf("  ---- Curtailment ----\n");
        printf("  Ratio: %5.1f%%  ResvUp: %6.1f MW  ResvDn: %6.1f MW\n",
               curtRatio*100, resvUp, resvDn);

        last_writes = writes;

        for (int i = 0; i < interval && !g_shutdown_requested; i++) sleep(1);
    }
    printf("\nShutting down...\n");
    atomic_store_explicit(&seg->header.shutdown_requested, true, memory_order_relaxed);
}

int main(int argc, char* argv[]) {
    (void)argc; // 避免未使用参数警告
    (void)argv; // 避免未使用参数警告

#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    printf("  ========================================\n");
    printf("  RT_DB Shared Memory Initializer\n");
    printf("  ========================================\n\n");
    
    // 设置信号处理
#ifdef _WIN32
    SetConsoleCtrlHandler(console_handler, TRUE);
#else
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
#endif
    
    void* shm_addr = NULL;
    int shm_id = -1;  // 在这里统一声明，避免重复声明
    (void)shm_id;      // Windows下不使用
    
#ifdef _WIN32
    // Windows平台初始化
    if (create_shared_memory_segment(&shm_addr) != 0) {
        return EXIT_FAILURE;
    }
    printf("Shared memory created successfully (Windows File Mapping)\n");
#else
    // Linux平台初始化
    key_t key = ftok("/tmp", 'R');
    if (key == -1) {
        perror("ftok failed");
        return EXIT_FAILURE;
    }
    
    shm_id = shmget(key, sizeof(SharedMemorySegment), IPC_CREAT | IPC_EXCL | 0666);
    if (shm_id == -1) {
        if (errno == EEXIST) {
            fprintf(stderr, "Shared memory already exists! Please stop existing instances first.\n");
            fprintf(stderr, "Or remove it manually with: ipcrm -M %d\n", key);
        } else {
            perror("shmget failed");
        }
        return EXIT_FAILURE;
    }
    
    shm_addr = shmat(shm_id, NULL, 0);
    if (shm_addr == (void*)-1) {
        perror("shmat failed");
        shmctl(shm_id, IPC_RMID, NULL);
        return EXIT_FAILURE;
    }
    
    printf("Shared memory created successfully (SHM ID: %d, Key: 0x%x)\n", shm_id, key);
#endif
    
    // 初始化共享内存段
    SharedMemorySegment* segment = (SharedMemorySegment*)shm_addr;
    initialize_shared_memory_segment(segment);
    
    // 开始监控
    monitor_shared_memory(segment);
    
    // 清理资源
    printf("Cleaning up shared memory...\n");
    
#ifdef _WIN32
    cleanup_shared_memory(shm_addr);
#else
    shmdt(shm_addr);
    shmctl(shm_id, IPC_RMID, NULL);
#endif
    
    printf("Cleanup completed. Goodbye!\n");
    return EXIT_SUCCESS;
}