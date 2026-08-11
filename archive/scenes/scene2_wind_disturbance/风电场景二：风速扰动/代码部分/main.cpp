#include "AGC.h"
#include "RtDbLogger.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#ifdef _WIN32
#include <windows.h>
#endif

using namespace std;

// 风速：12m/s → 12~15.5m/s（波动上升/下降）
double wind(int sce, double t) {
    if (sce == 1) {
        if (t < 20) return 12.0;
        else if (t < 80) return 13.5 + 1.2*sin(0.4*t) + 0.4*sin(2*t);
        else if (t < 90) return 14.0 - (t-80)*0.2;
        else return 12.0;
    }
    // sce == 2: 风速下降场景
    if (t < 20) return 12.0;
    else if (t < 80) return 10.5 + 1.0*sin(0.4*t) + 0.3*sin(2*t);
    else if (t < 90) return 10.0 + (t-80)*0.2;
    else return 12.0;
    return 12.0;
}

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    // ── 初始化共享内存 ──
    RtDbLogger rtDb;
    bool hasShm = rtDb.initialize();
    if (hasShm) {
        cout << "[共享内存] 已连接，实时数据可通过 rt_db_init.exe 监控" << endl;
    }

    WindFarmClusterControl farm;
    farm.add(4);
    AGC agc(&farm);

    cout << "========================================" << endl;
    cout << "  Wind Farm AGC Power Fluctuation Simulation" << endl;
    cout << "  (集成共享内存 RT_DB)" << endl;
    cout << "========================================" << endl;
    cout << "1. 风速上升波动 (12->15.5m/s)" << endl;
    cout << "2. 风速下降波动 (12->10m/s)" << endl;
    cout << "选择场景: ";
    int sce; cin >> sce;

    bool warn = false, stable = false;
    double maxFluctuation = 0.0;   // 跟踪最大功率波动
    double totalFluctuation = 0.0;
    int    fluctuationCount = 0;

    cout << fixed << setprecision(2);
    cout << "\nTime(s)\tWind(m/s)\tMode\t\t\tTarget\tActual\tUpMargin\tDownMargin\tPitch\tSpeed\n";
    cout << string(120, '-') << endl;

    for (double t = 0; t <= 120; t += 1.0) {
        double ws = wind(sce, t);
        farm.setWind(ws);

        if (t >= 18.0 && !warn) {
            cout << "--- 波动预警 → 平抑模式 ---\n";
            agc.enterSuppress(sce == 1);
            warn = true;
        }
        if (t >= 80.0 && !stable) {
            cout << "--- 风速稳定 → 经济模式 ---\n";
            agc.backEco();
            stable = true;
        }

        if (agc.mode() == SUPPRESS) {
            agc.run(0.05, sce == 1);
        } else {
            farm.resetAllSmooth();
            farm.distribute(10.0, 0.05, false);
        }

        auto fst = farm.status();

        // ── 写入共享内存 ──
        if (hasShm) {
            rtDb.logStep(t, ws, (int)agc.mode(), agc.target(),
                         fst.totalActual, fst.totalUp, fst.totalDown,
                         fst.avgPitch, fst.avgSpeed,
                         (agc.mode() == SUPPRESS));

            // 每5秒记录每台风机详细状态到共享内存
            if ((int)t % 5 == 0) {
                for (int i = 0; i < farm.turbineCount(); i++) {
                    auto* wt = farm.turbine(i);
                    if (wt) {
                        auto ts = wt->status();
                        rtDb.logTurbineDetail(i, ts.pitchAngle, ts.rotorSpeed,
                                              ts.adjustMarginUp, ts.adjustMarginDown,
                                              ts.actualPower);
                    }
                }
            }
        }

        // 追踪最大波动
        double fluctuation = fabs(fst.totalActual - agc.target());
        if (fluctuation > maxFluctuation) maxFluctuation = fluctuation;
        if (agc.mode() == SUPPRESS) {
            totalFluctuation += fluctuation;
            fluctuationCount++;
        }

        cout << setw(6) << t
             << setw(10) << ws
             << "\t" << left << setw(24) << agc.modeStr() << right
             << setw(6) << agc.target()
             << setw(8) << fst.totalActual
             << setw(10) << fst.totalUp
             << setw(12) << fst.totalDown
             << setw(8) << fst.avgPitch
             << setw(10) << fst.avgSpeed
             << endl;
    }

    // ── 写入最终摘要 ──
    double avgFluctuation = (fluctuationCount > 0) ? totalFluctuation / fluctuationCount : 0.0;
    double suppressionEff = (maxFluctuation > 0) ? (1.0 - avgFluctuation / maxFluctuation) : 0.0;
    if (hasShm) {
        rtDb.logFinalSummary(maxFluctuation, suppressionEff);
    }

    cout << "\n=== 扰动平抑性能摘要 ===" << endl;
    cout << "最大功率波动: " << maxFluctuation << " MW" << endl;
    cout << "平均波动(平抑模式): " << avgFluctuation << " MW" << endl;
    cout << "平抑效率: " << suppressionEff * 100.0 << "%" << endl;

    if (hasShm)
        cout << "[共享内存] 数据已持久化，可通过 rt_db_init.exe 终端查看" << endl;

    return 0;
}
