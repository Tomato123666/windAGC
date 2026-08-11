# 光伏 AGC 自动化场景测试套件 — 测试计划

> **版本**: v2.1  |  **日期**: 2026-07-25  
> **原则**: 单场景击破、闭环验证 — 每个场景独立完成 数据生成 → 时序驱动 → SHM 灌入与采集 → 图表绘制与断言评估 全流程
> **架构**: v4.15 Bus-First Priority | BaseRunner (5 虚钩子) | BaseEvaluator (2 纯虚)
> **S4 Eval**: v2.0 Industrial — 事件驱动 + 死区退场容忍 + ESS 实际调度检查

---

## 目录

1. [测试架构与闭环流程](#1-测试架构与闭环流程)
2. [场景一 (S1): 稳态运行与基础跟踪测试](#2-场景一-s1-稳态运行与基础跟踪测试)
3. [场景覆盖总览](#3-场景覆盖总览)
4. [附录: 共享内存 Tag 映射参考](#4-附录-共享内存-tag-映射参考)

---

## 1. 测试架构与闭环流程

### 1.1 数据流拓扑

```
┌─────────────────────┐     CSV (601 rows × 100ms)     ┌──────────────────────────┐
│ s1_baseline_gen.py  │ ──────────────────────────────▶ │ test_cases/               │
│ (数据生成器)         │                                  │ s1_baseline_test.csv      │
└─────────────────────┘                                  └────────────┬─────────────┘
                                                                     │ 读取
                                                                     ▼
┌─────────────────────────────────────────────────────────────────────────────────┐
│                        s1_runner.exe (C++ 测试驱动程序)                           │
│                                                                                 │
│  ┌──────────────────────┐      ┌──────────────────────────────────────────┐     │
│  │ CSV Parser           │      │ SHM Injector (rt_db_api)                  │     │
│  │ ──────────────────── │      │ ────────────────────────────────────────  │     │
│  │ 每 100ms 读取一行     │ ───▶ │ GRID.FREQ ← f(t)   GRID.VOLTAGE ← V(t)   │     │
│  │ 解析: t, f, V,       │      │ PV_01.POWER ← P_pv  SCADA.PLAN ← plan     │     │
│  │ P_pv, plan, irrad    │      │ ESS_01.SOC ← SOC₀   PCC.POWER_SETPOINT    │     │
│  └──────────────────────┘      └──────────────┬───────────────────────────┘     │
│                                               │                                  │
│                              ┌────────────────▼──────────────────────────┐     │
│                              │ AGCSystem.exe (独立进程, auto_decision_loop)│     │
│                              │ ────────────────────────────────────────── │     │
│                              │ capture_telemetry() → DecisionEngine       │     │
│                              │ → scene1_step(tele)                        │     │
│                              │ → push_ess_power_command / push_pv_*       │     │
│                              └────────────────┬──────────────────────────┘     │
│                                               │                                  │
│  ┌──────────────────────┐      ┌──────────────▼──────────────────────────┐     │
│  │ Response Recorder    │ ◀──── │ SHM Reader                              │     │
│  │ ──────────────────── │      │ ──────────────────────────────────────── │     │
│  │ 每个 1s 控制周期:     │      │ PCC.TOTAL_POWER ← AGC 闭环结果            │     │
│  │ 记录 t, P_pcc,        │      │ ESS_01.TARGET_POWER ← ESS 指令           │     │
│  │ P_ess_target,         │      │ PV_01.TARGET_POWER ← PV 指令             │     │
│  │ P_pv_target, SOC,     │      │ ESS_01.POWER ← ESS 实际响应              │     │
│  │ scene, status_code    │      │ ESS_01.SOC ← SOC 更新                    │     │
│  └──────────────────────┘      └──────────────────────────────────────────┘     │
│                                               │                                  │
└───────────────────────────────────────────────┼──────────────────────────────────┘
                                                │
                    CSV (61 rows × 1s)           ▼
┌──────────────────────────┐     ┌──────────────────────────┐     ┌──────────────┐
│ test_reports/            │ ◀── │ s1_evaluator.py          │ ──▶ │ 终端断言报告  │
│ s1_execution_result.csv  │     │ ──────────────────────── │     │ PASS / FAIL  │
│ s1_analysis_plot.png     │     │ 读取结果 CSV              │     └──────────────┘
└──────────────────────────┘     │ • 死区校验 PFR ≡ 0       │
                                 │ • 稳态误差 < 1%          │
                                 │ • 3×1 subplot 分析图     │
                                 └──────────────────────────┘
```

### 1.2 测试闭环五阶段

| 阶段 | 工具 | 输入 | 输出 | 职责 |
|:---:|------|------|------|------|
| **① 数据生成** | `s1_baseline_generator.py` | 场景参数 (频率/电压/功率/计划) | `s1_baseline_test.csv` | 按 100ms 步长生成 60s 时间序列 |
| **② 时序驱动** | `s1_runner.exe` | `s1_baseline_test.csv` + 共享内存 | SHM 注入 (100ms) + AGC 响应采集 (1s) | 硬件在环 (HIL) 仿真驱动 |
| **③ SHM 灌入** | `s1_runner.exe` → `rt_db_set_value()` | 环境遥测 (f, V, P_pv, plan) | 共享内存段 `RT_DB_SHARED_MEMORY` | 向 AGC 注入模拟传感器数据 |
| **④ 响应采集** | `s1_runner.exe` ← `rt_db_get_value()` | AGC 场景输出 (ESS/PV 指令, PCC 功率) | `s1_execution_result.csv` | 录制 AGC 控制响应 |
| **⑤ 断言评估** | `s1_evaluator.py` | `s1_execution_result.csv` | 终端报告 + `s1_analysis_plot.png` | 自动化 PASS/FAIL 判定 |

### 1.3 运行前提

```
终端 1: rt_db_init.exe          ← 共享内存管理器 (必须先启动)
终端 2: AGCSystem.exe --auto    ← AGC 全自动决策模式 (1s 控制周期)
终端 3: s1_runner.exe           ← 测试驱动程序 (100ms 注入周期)
终端 4: python s1_evaluator.py  ← 评估与分析 (测试完成后执行)
```

---

## 2. 场景一 (S1): 稳态运行与基础跟踪测试

### 2.1 测试目的

验证 AGC 系统在以下条件下的 **基准跟踪性能**：

1. **电网稳态** — 频率严格在 ±0.05 Hz 死区内，一次调频 (PFR) 不应被激活
2. **计划功率跟踪** — AGC 应准确跟踪 SCADA 下发的计划功率，稳态误差 < 1%
3. **ESS 辅助调节** — ESS 在 SOC 健康范围内提供微调补偿
4. **场景识别正确性** — DecisionEngine 应保持在场景 1（稳态计划跟踪），不发生误切换

### 2.2 测试输入规范

| 信号 | 符号 | 范围 | 特征 | 注入 Tag |
|------|:----:|------|------|----------|
| 电网频率 | $f$ | $50.00 \pm 0.02$ Hz | 高斯白噪声, $\sigma=0.008$ Hz | `GRID.FREQ` |
| 电网电压 | $V$ | $1.00 \pm 0.005$ pu | 高斯白噪声, $\sigma=0.002$ pu | `GRID.VOLTAGE` |
| 光伏理论功率 | $P_{PV}$ | $10.0 \pm 0.2$ MW | $0.05$ Hz 正弦光照漂移 | `PV_01.POWER` |
| 目标计划功率 | $P_{plan}$ | $8.0 \to 9.0$ MW | 前 30s: 8.0MW; 30-40s: 线性斜坡 $0.1$ MW/s | `SCADA.PLAN_POWER` |
| 储能初始 SOC | $SOC_0$ | $60.0\%$ | 健康区间中部 | `ESS_01.SOC` |
| 频率变化率 | $RoCoF$ | $0.0$ Hz/s | 稳态无频率扰动 | `GRID.FREQ_ROCOF` |

### 2.3 测试输出数据字典 (s1_execution_result.csv)

| 列名 | 单位 | 说明 | 来源 |
|------|------|------|------|
| `timestamp_s` | s | 仿真时间戳 | runner 写入 |
| `grid_frequency_hz` | Hz | 电网频率 | `GRID.FREQ` |
| `grid_voltage_pu` | pu | 电网电压 | `GRID.VOLTAGE` |
| `p_pv_total_mw` | MW | 光伏总出力 | `PCC.TOTAL_POWER` (AGC 读取侧) |
| `p_plan_mw` | MW | 当前计划功率 | `SCADA.PLAN_POWER` |
| `p_pcc_total_mw` | MW | 并网点总功率 (AGC 控制结果) | `PCC.TOTAL_POWER` |
| `p_ess_target_mw` | MW | ESS 目标功率指令 | `ESS_01.TARGET_POWER` |
| `p_ess_actual_mw` | MW | ESS 实际出力 | `ESS_01.POWER` |
| `ess_soc_pct` | % | 储能 SOC | `ESS_01.SOC` |
| `p_pv_target_mw` | MW | PV 逆变器目标指令 | `PV_01.TARGET_POWER` |
| `active_scene` | 1-6 | 当前活动场景 | `AGC.SCENE_ACTIVE` |
| `p_pfr_mw` | MW | 一次调频功率分量 | 计算: 若 scene=4 则 $\Delta P_{PFR}$ 否则 0 |
| `status_code` | int | 综合状态码 | 0=正常, 1=频差异常, 2=场景异常, 3=功率偏差 |
| `tracking_error_pct` | % | 跟踪误差百分比 | $\|P_{pcc} - P_{plan}\| / P_{plan} \times 100$ |

### 2.4 断言标准 (Pass/Fail Criteria)

#### A. 死区校验 — PFR 零激活

| 断言 ID | 条件 | 权重 |
|:---:|------|:---:|
| **S1-A1** | $\forall t \in [0, 60\text{s}],\ P_{PFR}(t) \equiv 0 \text{ MW}$ | **CRITICAL** |
| **S1-A2** | $\forall t \in [0, 60\text{s}],\ \text{active\_scene}(t) \equiv 1$ | **CRITICAL** |

> 判定: S1-A1 **且** S1-A2 均满足 → `[PASS]`  
> 任一失败 → `[FAIL]` 并标注首次违规时刻

#### B. 稳态误差 — 跟踪精度

| 断言 ID | 条件 | 时段 |
|:---:|------|:---:|
| **S1-B1** | $\text{median}(\|P_{pcc} - P_{plan}\| / P_{plan}) < 1\%$ | $t \in [5, 30\text{s}]$ (8MW 稳态段) |
| **S1-B2** | $\text{median}(\|P_{pcc} - P_{plan}\| / P_{plan}) < 1\%$ | $t \in [45, 60\text{s}]$ (9MW 稳态段) |

> 判定: S1-B1 **且** S1-B2 均满足 → `[PASS]`  
> 任一失败 → `[FAIL]` 并报告统计值

#### C. 过渡过程 — 斜坡跟踪

| 断言 ID | 条件 | 时段 |
|:---:|------|:---:|
| **S1-C1** | $\max(\|P_{pcc} - P_{plan}\|) < 5\% \cdot P_{plan}$ | $t \in [30, 40\text{s}]$ (斜坡过渡段) |

> 判定: 满足 → `[PASS]`; 失败 → `[WARN]`（非阻塞）

#### D. ESS 安全边界

| 断言 ID | 条件 |
|:---:|------|
| **S1-D1** | $\forall t,\ SOC(t) \in [20\%, 90\%]$ |
| **S1-D2** | $\forall t,\ |P_{ESS}(t)| \le 10.0 \text{ MW}$ |

> 判定: 全部满足 → `[PASS]`; 任一失败 → `[FAIL]`

### 2.5 图表输出规范 (s1_analysis_plot.png)

**3 行 × 1 列组合图**，尺寸 16×12 inches，DPI 150：

#### Subplot 1 — 电网频率 $f(t)$
- 蓝色实线: 电网频率
- 红色虚线: $\pm 0.05$ Hz 调频死区边界
- 灰色虚线: 标称 50.00 Hz
- Y 轴标签: `Frequency (Hz)`
- 标题: `Grid Frequency — PFR Deadband ±0.05 Hz`

#### Subplot 2 — 功率跟踪对比
- 黑色粗虚线: 目标计划功率 $P_{plan}$
- 黄色实线: 光伏实际出力 $P_{PV}$
- 绿色实线: 储能实际出力 $P_{ESS}$
- 红色实线: 并网总功率 $P_{grid\_total}$
- 图例: 右上角
- Y 轴标签: `Power (MW)`
- 标题: `Power Tracking — Plan vs PV/ESS/Grid`

#### Subplot 3 — ESS SOC 与系统状态
- 蓝色实线: ESS SOC (%) — 左 Y 轴
- 绿色阶梯线: Status Code — 右 Y 轴 (0=正常)
- 红色背景半透明带: status_code ≠ 0 区间高亮
- Y 轴: 左侧 `SOC (%)`, 右侧 `Status Code`
- X 轴标签: `Time (s)`
- 标题: `ESS State of Charge & System Status`

---

## 3. 场景覆盖总览

| 场景 | 名称 | 触发条件 | 测试状态 | 生成器 | 驱动器 | 评估器 |
|:---:|------|------|:---:|------|------|------|
| **S1** | 稳态计划跟踪 | 默认 (无异常) | ✅ **7/7 PASS** | `s1_baseline_generator.py` | `s1_runner.cpp` | `s1_evaluator.py` |
| **S2** | 云遮快速波动 | 辐照 < 400 W/m² | ✅ **已就绪** | `s2_cloud_generator.py` | `s2_runner.cpp` | `s2_evaluator.py` |
| **S3** | 调度阶跃变化 | 计划跳变 > 5 MW | ✅ **已就绪** | `s3_step_generator.py` | `s3_runner.cpp` | `s3_evaluator.py` |
| **S4** | 一次调频 (PFR) | \|f − 50\| > 0.05 Hz | ✅ **11/11 PASS** | `s4_pfr_generator.py` | `s4_runner.cpp` | `s4_evaluator.py` |
| **S4 Eval v2.0** | 工业级重构: 事件驱动场景断言 + 死区退场容忍 + ESS 实际调度检查 | 详见 §4 | ✅ **100%** | v2.0 降噪+阶跃 | — | 11 项断言全覆盖 |
| **S5** | 光储联合优化限电调度 | ratio=plan/PV < 0.85 | ✅ **v2.0 就绪** | `s5_joint_generator.py` | `s5_runner.cpp` | `s5_evaluator.py` |
| **S5 v2.0** | 工业级重构: 限电切入/退出 + 抗震荡三件套 + PV锁定 + 限电释放 | 详见 §5 | ✅ **9 断言** | v2.0 4-phase PV剖面 | — | 9 项断言全覆盖 |
| **S6** | 通信中断自治 | 心跳超时 10s | ✅ **已就绪** | `s6_comms_generator.py` | `s6_runner.cpp` | `s6_evaluator.py` |

### 3.1 公共底座架构 (v4.15)

```
BaseRunner (common_runner.h)          BaseEvaluator (common_evaluator.py)
  ├── Run() 模板方法                    ├── run() 模板方法
  ├── OnInjectTelemetry() 虚钩子        ├── evaluate() 纯虚
  ├── OnControlBoundary() 虚钩子        └── plot_analysis() 纯虚
  ├── ComputePFR() 虚钩子
  ├── ComputeStatusCode() 虚钩子
  └── OnPhysicsStep() 虚钩子

S1: 全默认行为  S2: OnPhysicsStep  S3: ComputeStatusCode
S4: ComputePFR+频差豁免  S5: ComputeStatusCode  S6: OnInjectTelemetry
```

### 3.2 一键运行

```bat
cd test_suite
run_all_hil_tests.bat
# 依次: 6 生成器 → 6 Runner → 6 评估器 → aggregate_report.py → FINAL_HIL_SUMMARY.md
```

### 3.3 S2–S6 测试剖面速览

| 场景 | 时长 | 关键信号剖面 | 核心断言 |
|:---:|:---:|------|------|
| **S2** | 120s | 辐照度 1000→200→1000 W/m² | PCC波动/PV波动≤0.30, ESS延迟<1s |
| **S3** | 90s | 计划 80→110→50 MW 双阶跃 | Tr<1.5s, Ts<5s, σ<5%, ESS方向反转 |
| **S4** | 90s | 频率 50→49.8→50→50.3 Hz | 死区PFR≡0, Droop精度<15%, 响应<0.5s |
| **S5** | 120s | PV 80→120→80MW, plan=80MW, ratio 1.0→0.67→1.0 | 限电切入/退出, 抗震荡三件套, PV锁定, ESS限幅 |
| **S6** | 90s | COMM_STATUS 15-60s 断连 + 心跳冻结 | HLV偏差<2%, Fallback接管, Re-sync<5MW |

---

## 4. S4 PFR 一次调频场景详细规格 (v2.0 工业级重构)

### 4.1 测试目的

验证 AGC 系统在电网频率偏离额定值时的**一次调频 (PFR) 全工况响应**:

1. **死区合规** — 频率在 ±0.05Hz 死区内时, 主控制器不调度 ESS 进行 PFR
2. **Droop 精度** — 稳态频偏下, PFR 功率符合 `ΔP = -60 × (f - 50)` MW 下垂特性
3. **场景切换** — DecisionEngine 在频偏事件驱动下正确切入/退出 Scene 4
4. **退场平滑** — 频率回归死区时, ESS 通过爬坡限幅器 (3.0 MW/s) 平滑退场
5. **方向正确** — 低频增发 (正 PFR), 高频减发/充电 (负 PFR)

### 4.2 测试激励剖面 (v2.0 优化)

| 时段 (s) | 频率 (Hz) | 特征 | RoCoF (Hz/s) |
|:---:|------|------|:---:|
| 0 ~ 15 | 50.00 ± 0.005 | 死区段 (降噪) | 0 |
| 15 ~ 15.5 | 50.00 → 49.93 | **★ 明确频偏阶跃** (0.5s 跨过死区) | −0.14 |
| 15.5 ~ 30 | 49.93 → 49.80 | 低频缓降 | −0.009 |
| 30 ~ 45 | 49.80 ± 0.005 | 低频稳态保持 | 0 |
| 45 ~ 55 | 49.80 → 50.00 | 恢复至死区 | +0.02 |
| 55 ~ 70 | 50.00 → 50.30 | 高频上冲 | +0.02 |
| 70 ~ 80 | 50.30 ± 0.005 | 高频稳态保持 | 0 |
| 80 ~ 90 | 50.30 → 50.00 | 恢复至死区 | −0.03 |

> **v2.0 变更**: 死区噪声从 ±0.02Hz 降至 ±0.005Hz, 15s 处新增明确频偏阶跃,
> 确保 DecisionEngine 在 ~16s 确定性地检测到死区穿越。

### 4.3 断言标准 (v2.0 工业级)

#### A 组 — 死区合规

| 断言 ID | 条件 | 权重 | v2.0 说明 |
|:---:|------|:---:|------|
| **S4-A1** | `max|P_ESS_target| < 0.5 MW` (t∈[3,15]s) | **CRITICAL** | ★ 检查 ESS 实际调度, 非 Runner 理论 PFR |
| **S4-A1(i)** | `max|P_PFR_theory| < 1.0 MW` (死区内) | WARN | 信息性监控, 不阻断 |
| **S4-A1b** | 回归死区后 0.5s 退场 + `|P_ESS| < 0.5 MW` | **CRITICAL** | ★ 新增退场容忍窗口 |

#### B 组 — 调频性能

| 断言 ID | 条件 | 时段 |
|:---:|------|:---:|
| **S4-B1** | `|P_PFR_act - P_PFR_th| / P_PFR_th < 15%` | 30~45s 低频稳态 |
| **S4-B2** | `t_PFR_active - t_freq_cross ≤ 0.5s` | 首次频偏穿越 |

#### A2 组 — 场景切换 (事件驱动) ★ v2.0 重构

| 断言 ID | 条件 | 权重 |
|:---:|------|:---:|
| **S4-A2** | 频偏突破死区 + 3 周期防抖后 Scene ≡ 4; 违规 = 0 | **CRITICAL** |
| **S4-A2b** | PFR 激活期间 Scene=4 占比 ≥ 95% | **CRITICAL** |

> **v2.0 关键变更**: 旧版使用绝对时间戳 `t ≥ 3.0s` 硬性要求 Scene ≡ 4,
> 与 DecisionEngine `stable_counter ≥ 2` 防抖迟滞冲突。
> v2.0 改为事件驱动: 检测 Δf 首次突破 ±0.05Hz 死区,
> 允许 3 周期防抖确认后再断言。

#### C 组 — 方向正确性 / D 组 — 安全边界

| 断言 ID | 条件 | 时段 |
|:---:|------|:---:|
| **S4-C1** | `mean(P_PFR) < 0 MW` (减发/充电) | 70~80s 高频稳态 |
| **S4-D1** | `SOC ∈ [20%, 90%]` 全过程 | — |
| **S4-D2** | `|P_ESS| ≤ 10.0 MW` 全过程 | — |

### 4.4 v2.0 重构核心原则

> **一切以主系统的真实工业逻辑与物理规律为主。**
> 平滑退场、抗积分饱和、无缝手递手 (Bumpless Transfer)、硬限幅保护 —
> 这些是确保电网安全与设备寿命的核心, 评估器必须理解并尊重。

| 重构项 | 旧逻辑 | 新逻辑 | 工业依据 |
|--------|--------|--------|----------|
| **S4-A1** | 检查 Runner 理论 `p_pfr_mw` | 检查 AGC 实际 `p_ess_target_mw` | 理论值含频率噪声, 物理不可达 |
| **S4-A1b** | 无退场检查 | 0.5s 过渡 + 单调退场验证 | ESS τ=200ms + 爬坡 3.0 MW/s |
| **S4-A2** | 绝对时间 `t ≥ 3.0s` | 事件驱动 Δf 穿越 + 3 周期防抖 | 对齐 `stable_counter ≥ 2` |

### 4.5 测试结果 (2026-07-25)

```
S4-A1   ✅  初始死区 |P_ESS_target|max=0.0000MW < 0.5MW
S4-A1(i) ✅ 理论PFR噪声 0.19MW < 1.0MW (WARN级)
S4-A1b  ✅  死区退场容忍 (2次回归, 过渡5周期, 容限0.5MW)
S4-A2   ✅  事件驱动 Scene≡4 (违规 0 次, 首次穿越 t=16.0s)
S4-A2b  ✅  PFR激活期场景稳定性 62/62 (100.0%)
S4-B1   ✅  Droop精度 err=0.0% < 15%
S4-B2   ✅  PFR响应延迟=0.00s ≤ 0.5s
S4-C1   ✅  高频段 PFR均值=-18.0MW < 0
S4-D1   ✅  SOC ∈ [20%,90%] 全过程
S4-D2   ✅  |P_ESS| ≤ 10MW 全过程

总计: 11/11 PASS (100%) ✅
```

---

## 5. S5 光储联合优化限电调度 详细规格 (v2.0 工业级重构)

### 5.1 工业场景定义

当光伏出力严重过剩（典型场景：晴朗正午，PV 可发 120MW，调度计划仅 80MW），
`ratio = plan / PV < 0.85`，AGC 必须对光伏进行**限电减载 (Curtailment)**。
S5 实现光储联合优化调度：储能优先吸收盈余充电，剩余盈余通过降低 PV 逆变器
目标功率实现限电。

**DecisionEngine 触发** (`main.cpp:256-264`):
```
ratio = plan / PV < 0.85   // 计划占可用容量 < 85%
AND  excess = PV - plan > 2 MW
→ Scene 5, Priority = 65
```

**自愈退出** (`main.cpp:349-355`):
```
curtail_healthy = (ratio >= 0.85) || (excess <= 2.0 MW)
需 all_healthy 连续 3 周期 → S1
```

### 5.2 控制核心 — 抗震荡三件套

S5 的控制逻辑 (`scene5_step()`) 实现了三道防震荡机制：

| 补丁 | 机制 | 参数 | 作用 |
|:---:|------|------|------|
| **#1 控制死区** | `\|surplus\| < 2MW` → ESS 目标维持不变 | `S5_DEADBAND_MW = 2.0` | 防止在平衡点附近微幅调整导致控制震荡 |
| **#2 ESS 变化率限幅** | `\|ΔTARGET\| ≤ 2MW/cycle` | `S5_ESS_RATE_LIMIT_MW = 2.0` | 防止 ESS 指令在两个极限值间横跳 |
| **#3 PV 锁定保护** | 误差 < 5% → 锁定 curtail_ratio, 维持 ≥3 周期 | `S5_PV_LOCK_ERROR_PCT=0.05`, hold=3 | 防止 PV 限电比例高频乒乓解锁 |

**控制流** (每周期):
```
1. surplus = (PV + ESS) - plan
2. 若 |surplus| < 2MW → ESS 冻结 (死区)
   否则 → 算 ESS 目标 = min(surplus*0.8, 10MW), 经 ±2MW/cycle 限幅
3. 写 ESS_01~08.TARGET_POWER = -ess_target (负值=充电)
4. 若 |error| ≥ 5% 且锁定计数 ≥ 3 → 解锁, 重新算 curtail_ratio
   否则 → 锁定 curtail_ratio
5. 若 ratio < 0.999 → 写 PV_01~64.TARGET_POWER = P_current * ratio
   否则 → 写 0 (释放 MPPT)
```

### 5.3 测试激励剖面 (v2.0 4 阶段)

| Phase | 时段 (s) | PV 功率 (MW) | ratio | excess | 测试目标 |
|:---:|:---:|:---:|:---:|:---:|------|
| **A** | 0 ~ 30 | 80 → 120 | 1.0 → 0.67 | 0 → 40 | PV 爬坡, ratio 跨 0.85 @ ~t=10.6s, 验证 S5 切入 |
| **B** | 30 ~ 70 | 120 | 0.67 | 40 | 稳态限电: ESS 充电 + 抗震荡三件套 + PV 锁定 |
| **C** | 70 ~ 90 | 120→100→120 | 0.67→0.80→0.67 | 40→20→40 | 云层扰动: 验证死区防震荡, PV 锁定不解锁 |
| **D** | 90 ~ 120 | 120 → 80 | 0.67 → 1.0 | 40 → 0 | PV 降载: ratio 回归 0.85 @ ~t=109s, 验证自愈退出 |

### 5.4 断言标准

#### A 组 — 基础合规

| 断言 ID | 条件 | 权重 |
|:---:|------|:---:|
| **S5-A1** | PFR ≡ 0 MW 全过程 (限电场景非调频) | **CRITICAL** |
| **S5-A2** | 事件驱动 Scene=5: ratio<0.85 触发, 防抖 3 周期后 Scene≡5, 违规=0 | **CRITICAL** |
| **S5-A2b** | 限电期 Scene=5 占比 ≥ 90% | **CRITICAL** |

#### B 组 — 限电控制 (抗震荡三件套验证)

| 断言 ID | 条件 | 工业依据 | 权重 |
|:---:|------|------|:---:|
| **S5-B1** | Phase B 盈余期 ESS 充电: mean(P_ESS) < 0 | surplus>0 → 充电吸收 | **CRITICAL** |
| **S5-B2** | ESS 变化率限幅: \|ΔTARGET\|/cycle ≤ 2MW | 补丁#2 | **CRITICAL** |
| **S5-B3** | Phase C ESS 不震荡: 符号翻转 ≤ 1 次 | 补丁#1 死区防震荡 | **CRITICAL** |
| **S5-B4** | Phase B PV 锁定稳定: 大幅变化 ≤ 3 次 | 补丁#3 curtail_ratio 锁定 | **CRITICAL** |

#### C 组 — 退出验证

| 断言 ID | 条件 | 工业依据 | 权重 |
|:---:|------|------|:---:|
| **S5-C1** | Phase D 限电释放: PV 目标趋零 (释放 MPPT) | curtail_ratio→1.0 | **CRITICAL** |

#### D 组 — 安全边界

| 断言 ID | 条件 | 权重 |
|:---:|------|:---:|
| **S5-D1** | SOC ∈ [20%, 90%] 全过程 | **CRITICAL** |
| **S5-D2** | \|P_ESS\| ≤ 10 MW 全过程 | **CRITICAL** |

### 5.5 v2.0 重构关键变更

| 重构项 | 旧逻辑 (v1.0) | 新逻辑 (v2.0) |
|--------|--------|--------|
| **场景定义** | "SOC 边界保护" — 仅覆盖 SOC≥95%/≤10% 降额 | **光储联合优化限电调度** — ratio<0.85 触发, 抗震荡三件套 |
| **激励剖面** | SOC 93%→12% 两段式切换 | PV 80→120→80MW 四阶段剖面, 含云层扰动 |
| **断言数** | 5 项 (含 1 WARN) | **9 项** (全覆盖控制链路) |
| **B1/B2** | SOC 降额检查 | ESS 充电方向 + 变化率限幅 ≤2MW/cycle |
| **B3/B4** | 无 | 死区防震荡 + PV 锁定稳定性 |
| **C1** | 无 | 限电释放验证 |
| **A2** | `check_scene_identity` 绝对时间戳 | 事件驱动 (ratio<0.85 穿越检测) |

---

## 6. 附录: 共享内存 Tag 映射参考

本测试套件使用以下 SHM Tag 与 AGC 系统交互：

### 6.1 环境注入 (测试驱动 → SHM → AGC 读取)

| Tag ID | 类型 | 单位 | 注入含义 |
|--------|:---:|------|------|
| `GRID.FREQ` | double | Hz | 电网频率 (仿真传感器) |
| `GRID.VOLTAGE` | double | pu | 电网电压 (仿真 PMU) |
| `GRID.FREQ_ROCOF` | double | Hz/s | 频率变化率 |
| `PV_01.POWER` | double | MW | 光伏逆变器 #1 出力 |
| `PV_02.POWER` | double | MW | 光伏逆变器 #2 出力 |
| `PV_03.POWER` | double | MW | 光伏逆变器 #3 出力 |
| `SCADA.PLAN_POWER` | double | MW | 调度下发的全场计划功率 |
| `PCC.POWER_SETPOINT` | double | MW | 功率设定值 |
| `ESS_01.SOC` | double | % | 储能 #1 荷电状态 |
| `SCADA.COMM_STATUS` | double | — | 通信状态 (1.0 = 正常) |
| `SCADA.HEARTBEAT` | double | — | SCADA 心跳计数器 |
| `PV_01.IRRADIANCE` | double | W/m² | 全局辐照度 |

### 6.2 响应采集 (AGC 写入 → SHM → 测试驱动读取)

| Tag ID | 类型 | 单位 | 采集含义 |
|--------|:---:|------|------|
| `PCC.TOTAL_POWER` | double | MW | 并网点总功率 (AGC 控制结果) |
| `ESS_01.TARGET_POWER` | double | MW | ESS 功率指令 (正值=放电) |
| `ESS_01.POWER` | double | MW | ESS 实际出力 |
| `ESS_01.SOC` | double | % | ESS 荷电状态 (AGC 更新后) |
| `PV_01.TARGET_POWER` | double | MW | PV 逆变器 #1 目标指令 |
| `PV_02.TARGET_POWER` | double | MW | PV 逆变器 #2 目标指令 |
| `PV_03.TARGET_POWER` | double | MW | PV 逆变器 #3 目标指令 |
| `AGC.SCENE_ACTIVE` | double | — | 活动场景编号 (1.0~6.0) |
