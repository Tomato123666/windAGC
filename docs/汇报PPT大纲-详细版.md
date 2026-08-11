# 风电场AGC多场景统一架构改造与系统集成 — 汇报PPT大纲（16页）

> 所有数据均来自系统实际运行输出，可逐项验证。

---

## 第1页 | 封面

**风电场AGC自动发电控制系统**
——多场景统一架构改造与智能工况切换系统

- 项目：新能源控制项目暑期研发
- 日期：2026年7月（更新：2026年8月）
- 代码规模：约100个源文件，约20,000行C++
- 交付物：统一AGC系统（Daemon模式）+ RT_DB共享内存 + HIL自动化测试框架 + 6场景独立模块

---

## 第2页 | 系统总体架构

**五层架构设计**

```
┌─────────────────────────────────────────────┐
│  HIL测试框架 (tools/data_generator/)          │
│  generator.py → runner.exe → AGC → evaluate  │
├─────────────────────────────────────────────┤
│  统一AGC层 (unified_agc/)  17文件 2000行       │
│  Daemon模式: ConditionDetector→6 Strategy→step│
├─────────────────────────────────────────────┤
│  公共基础层 (common/)  5文件 660行            │
│  CommonTypes + PID + Limiter + WindCurve     │
├─────────────────────────────────────────────┤
│  共享内存层 (RT_DB)  869数据点 256指令队列     │
│  seqlock无锁读写 + SPSC环形队列              │
├─────────────────────────────────────────────┤
│  6场景策略层 (archive/scenes/, 算法全部保留)   │
│  场景1:100台×3MW  场景2:4台×2.5MW ...         │
└─────────────────────────────────────────────┘
```

**设计原则**
- 算法与通信解耦：控制策略不感知数据总线
- 最小侵入改造：原场景算法逻辑零改动
- 策略模式设计：所有策略继承统一基类 `StrategyBase`，多态调用
- Daemon守护进程：持续运行，外部信号驱动，接近真实工业部署

---

## 第3页 | 统一类型系统与公共算法库

**CommonTypes.h — 合并6场景所有类型定义**
- 10组枚举：TurbineRunState / FarmControlMode / SafetySubMode / FRState / CurtailMode / CommandType / TurbineRole / ExtremeSubType / FanCurtailState / OperationMode
- 6组结构体：TurbineStatus（25字段）/ FarmStatus（25字段）/ DispatchCommand / TurbineCommand / ControlResult / PIDParams
- 合并前每场景独立定义，字段名不同、枚举值不同
- 合并后全系统一套类型，跨场景数据交换零歧义

**公共算法模块**

| 模块 | 合并来源 | 核心特性 |
|------|---------|---------|
| PIDController | 场景1/2/3/4/6 | 条件积分法抗饱和 + 微分低通滤波 + 输出限幅 |
| LowPassFilter | 场景1/5 | 一阶IIR，alpha可配置 |
| Limiter | 场景1/2/4/5 | clamp / deadband / rampLimit / rampLimitAsym |
| WindPowerCurve | 场景1/2/5 | 三次方/二次方/线性三种模型 + 桨距效率修正 |

**消除重复代码**
- PID实现：4份 → 1份 | 限幅函数：4份 → 1份
- 功率曲线：3份 → 1份 | 一阶滤波器：2份 → 1份
- 类型定义：6套互不兼容 → 1套统一

---

## 第4页 | RT_DB共享内存设计

**技术选型**
- 平台：Windows FileMapping（条件编译支持Linux SystemV）
- 并发：seqlock序列锁（无锁读）+ 原子操作（无锁写）
- 指令通道：SPSC无锁环形队列（256深度）
- 性能：P99写入<2μs，读吞吐>100K次/s，写吞吐>2K次/s，内存≈128KB

**数据点布局（869点）**

| Slot范围 | 命名空间 | 数量 | 用途 |
|----------|----------|------|------|
| 0-7 | BMS_01 / PCS_01 / SCADA | 8 | 储能BMS+变流器 |
| 8-18 | WIND_AGC | 11 | 场站级核心（功率/误差/MAE/RMSE） |
| 19-323 | TURBINE_NNN ×100 | 305 | 风机基础（Power/WindSpeed/Command） |
| 324-723 | TURBINE_NNN ×100 | 400 | 风机扩展（Pitch/Rotor/Up/DownMargin） |
| 724-830 | GRID + FR_CTRL + FR_Power | 107 | 电网频率 + 一次调频 |
| 831-853 | PV_AGC + CURTAIL + REST_ROTATE | 23 | 限电管理 + 轮休调度 |
| 854-880 | SAFETY + COMM + EXTREME + AVC + SCHEDULE + RAMP | 27 | 安全 + 通信 + 电压 + 调度 |

---

## 第5页 | 统一AGC核心流程

**Daemon 主循环（`main.cpp`）**

```
while (g_running) {
    从共享内存读取: freq / windSpeed / commHealthy / extremeType / curtailRatio / scheduleMW
        ↓
    agc.step(cmd, 1.0)  ─── 4行核心 ───
        ├─ ConditionDetector.detect()    工况检测（7级优先级）
        ├─ strategies_[scene-1]->step()  策略执行（多态调用）
        ├─ logger_->logFarmState()       写回共享内存
        └─ 更新FarmStatus                状态同步
        ↓
    sleep(100ms) → 下一周期
}
```

**运行模式演进**
- **旧版**: 硬编码24h脚本 `for (int step = 0; step < 1440; step++)` → 86秒固定流程后退出
- **新版**: Daemon守护进程 `while (g_running)` → 持续运行，外部信号注入驱动，无固定退出时间

**策略接口（`StrategyBase`）**
```cpp
class StrategyBase {
    virtual const char* name() const = 0;       // 策略名称
    virtual int sceneId() const = 0;            // 场景编号 1-6
    virtual ControlResult step(                 // 核心：每周期控制计算
        const DispatchCommand& cmd,
        FarmStatus& farm,
        double dtSec) = 0;
};
```

---

## 第6页 | 工况检测器：7级优先级决策

**ConditionDetector 决策树**

```
┌──────────────────────────────────────────────────┐
│ P6 [最高]  commHealthy==false?                     │
│            extremeType!=NONE? windSpeed>25?         │
│            turbulence>0.25?                         │
│            → 场景6 安全模式                          │
├──────────────────────────────────────────────────┤
│ P5          |freq-50Hz|>0.10?                     │
│            → 场景3 一次调频                          │
├──────────────────────────────────────────────────┤
│ P4          |windSpeed-last|>1.5m/s?              │
│            → 场景2 风速波动抑制                      │
├──────────────────────────────────────────────────┤
│ P3          curtailRatio > 40%?                   │
│            → 场景5 限电管理                          │
├──────────────────────────────────────────────────┤
│ P2          cmdType==STEP/RAMP? |ΔP|>5MW?         │
│            滞回出口: |ΔP|<1MW 才退出                │
│            → 场景4 爬坡跟踪                          │
├──────────────────────────────────────────────────┤
│ P1 [默认]                                         │
│            → 场景1 常规AGC                          │
└──────────────────────────────────────────────────┘
```

**设计要点**
- 安全永远是最高优先级，不可被其他场景覆盖
- 场景4有滞回机制（ΔP>5MW进入，ΔP<1MW退出），避免1↔4反复抖动
- 场景2持续跟踪风速变化，稳定后自动退出
- 场景3频率恢复至死带内后自动退出
- 所有阈值通过 `ConditionDetector::Config` 结构体可配置

---

## 第7页 | 场景1：常规AGC经济调度

| 属性 | 值 |
|------|-----|
| **策略名称** | Normal AGC (Eco-Dispatch) |
| **原始作者** | 王翰铭 |
| **规模** | 100台 × 3MW = 300MW |
| **核心算法** | 前馈-反馈PID + 效率加权经济调度 |
| **KPI** | RMSE ≤ 2% Pn，合格率 ≥ 95% |

**控制公式**

```
P_cmd = P_schedule + P_feedforward + P_feedback
```

**前馈通道（预测修正）**
- 输入：调度功率变化量 ΔP
- 低通滤波（α=0.15）平滑高频噪声
- 置信度调节：`conf = 0.85×(1 - |ΔP_filtered|/Pn×2)`，限幅 [0.3, 0.95]
- 输出：`ff = 0.75 × conf × ΔP_filtered`，限幅 ±15% Pn

**反馈通道（误差消除）**
- PID：Kp=1.2, Ki=0.15, Kd=0.03
- 积分限幅 [-20%Pn, +20%Pn]，输出限幅 ±20% Pn
- 死区：0.5% Pn（避免微幅振荡）
- 爬坡限制：1% Pn/s

**经济调度（效率加权分配）**
- 效率曲线基于负荷率：<10%→0.8, 10-70%→线性爬升, 70-90%→1.0峰值, >90%→0.95
- 分配策略：`P_i = P_min + (P_target - ΣP_min) × (w_i / Σw)`
- 权重：`w_i = P_availMax × η(safetyIndex)`

**演示验证点**
- [ ] 输出 "RT_DB connected, 311 data points mapped"
- [ ] 1200周期（20分钟）仿真不间断
- [ ] 最终统计：RMSE≤2%, 合格率≥95%
- [ ] 终端每100s输出状态摘要

---

## 第8页 | 场景2：风速波动抑制

| 属性 | 值 |
|------|-----|
| **策略名称** | Wind Disturbance Suppression |
| **原始作者** | 卓世杰 |
| **规模** | 4台 × 2.5MW = 10MW |
| **核心算法** | ECO/SUPPRESS双模式切换 + 桨距角PID |
| **KPI** | 波动抑制率 > 5% |

**双模式状态机**

```
       风速突变 > 0.5 m/s
  ECO ────────────────────→ SUPPRESS
   ↑                          │
   └──────────────────────────┘
      风速稳定（连续无突变）
```

**ECO模式（稳态）**
- 桨距角保持最优位置，风机以MPPT方式运行
- 波动监控持续跟踪风速变化量

**SUPPRESS模式（波动抑制）**
- 触发条件：`|ΔWindSpeed| > 0.5 m/s`
- 桨距角PID调节：
  - Kp=1.8, Ki=0.06, Kd=0.04
  - 输出限幅 [-0.5, +0.5] deg
- 桨距效率修正：`Power = P_avail × (1 - pitchAngle/8.0 × 0.22)`
  - 效率范围 [0.8, 1.0]
- 爬坡保护：桨距 0.3 deg/s，功率 2.5 MW/s
- 退出条件：连续无风速突变，递减计数器归零

**演示验证点**
- [ ] 输入下降风场景（选项2）
- [ ] t≈18s 风速突变→SUPPRESS触发
- [ ] 桨距角自动增大，功率波动被抑制
- [ ] t≈80s 风速稳定→恢复ECO

---

## 第9页 | 场景3：一次调频

| 属性 | 值 |
|------|-----|
| **策略名称** | Primary Frequency Regulation |
| **原始作者** | 郑怀勇 |
| **规模** | 10台 × 2.5MW = 25MW |
| **核心算法** | READY→ACTIVE→RECOVERY→READY 四状态FSM + PI控制 |
| **KPI** | < 30秒频率恢复至50Hz |

**四状态有限状态机**

```
         |Δf| > 0.10 Hz
  READY ────────────────→ ACTIVE
    ↑                       │
    │    |Δf| < 0.02 Hz     │
    │                       ↓
    │                   RECOVERY
    └───────────────────────┘
       |Δf| < 0.003 Hz
       (积分器归零)
```

**PI调频控制**

| 参数 | 值 | 说明 |
|------|-----|------|
| P项系数 | -30.0 | 频差比例响应，负号=频率下降则增发功率 |
| I项系数 | -0.8 | 累积频差消除稳态误差 |
| 积分累积 | `Σ Δf × 0.2`（仅|Δf|>0.005Hz时） | 避免死区内无效累积 |
| 输出限幅 | ±5% Pn（±1.25MW） | 保证调频不越限 |

**功率分配**
- 均匀分配给所有 NORMAL 状态的风机
- 单台限幅：[P_min, P_max]
- 频率恢复速率：由 UnifiedAGC::step() 模拟（每周期频率向50Hz收敛2%）

**状态转换日志**
```
[AGC] 1→3: Freq deviation 49.71 Hz    ← ACTIVE 触发
[AGC] 3→1: Freq recovered              ← RECOVERY→READY 完成
```

**演示验证点**
- [ ] 注入 49.7Hz 频率扰动
- [ ] READY→ACTIVE→RECOVERY→READY 完整状态序列
- [ ] <30秒频率恢复至50Hz
- [ ] 调频功率不超过 ±5% Pn

---

## 第10页 | 场景4：调度指令爬坡跟踪

| 属性 | 值 |
|------|-----|
| **策略名称** | Ramp Tracking (Step/Ramp) |
| **原始作者** | 李隆就 |
| **规模** | 2台 × 50MW = 100MW |
| **核心算法** | 预处理器 + PID补偿 + 机械保护 |
| **KPI** | 跟踪误差 < 0.5MW，扭矩/弯矩保护有效 |

**跟踪模式**

| 指令类型 | 行为 |
|----------|------|
| **STEP** | 瞬时跟踪目标功率，依赖PID补偿快速收敛 |
| **RAMP** | 按指定爬坡率（默认50MW/min）线性逼近目标 |

**预处理器（DesiredTarget计算）**
- STEP模式：`desiredTarget = targetPower`（直接设为目标）
- RAMP模式：`desiredTarget = start ± rampRate × elapsedSec`
  - 受 `targetPower` 上限约束
  - `rampRateMWmin` 支持外部配置（从 `DispatchCommand` 读取）

**PID补偿器**
- Kp=0.3, Ki=0（纯比例，避免斜坡跟踪超调）
- 积分限幅 [0.1, 5.0]，输出限幅 ±5MW
- 补偿公式：`compensated = clamp(desiredTarget + PID_output, 0, totalAvailable)`

**机械保护三重检查**
1. 扭矩保护：`torque < 1500 KNm × 1.1`
2. 弯矩保护：`bladeRootMoment < 5000 × 1.1`
3. 超限时：`result.success = false`，功率指令不更新

**滞回退出**
- 进入：`|ΔP| > 5MW`（ConditionDetector 触发）
- 退出：`|actual - target| < 0.5MW`（策略内部判断，写 "TRACKING_COMPLETE"）

**演示验证点**
- [ ] 30MW→70MW 阶跃指令
- [ ] 6秒内功率爬升至目标
- [ ] 稳态误差 < 0.5MW
- [ ] 扭矩/弯矩无超限报警

---

## 第11页 | 场景5：限电备用优化

| 属性 | 值 |
|------|-----|
| **策略名称** | Curtailment Reserve Optimization |
| **原始作者** | 赖嘉雯 |
| **规模** | 50台 × 2MW = 100MW |
| **核心算法** | 模式选择 + 安全指数评估 + 均匀限电分配 |
| **KPI** | 三种模式自动切换，安全指数保护有效 |

**限电模式决策**

```
限电比例 R = (P_avail - P_target) / P_avail

  R > 40% 且 持续 > 2h  →  DeepCurtail（深度限电）
  R < 30% 或 持续 < 1.5h →  NormalTrack（正常跟踪）
```

滞回区间（30%-40%）防止模式反复切换。

**风机安全指数 S**

```
S = 1.0
  × (vibration > 50 mm/s  → ×0.5)   // 振动超标
  × (gearbox > 80°C      → ×0.7)   // 齿轮箱过热
  × (genTemp > 90°C      → ×0.7)   // 发电机过热
```

- S < 0.2：风机自动切至 RESTRICTED 状态
- S > 0.6 且持续稳定：RESTRICTED → NORMAL
- 评估周期：每个 step() 调用

**功率分配策略**

| 模式 | 分配策略 |
|------|----------|
| **NormalTrack** | 每台风机出力 = 95% P_availMax（接近MPPT） |
| **DeepCurtail** | 按高效功率点（90% P_availMax）等比分配，保底不低于 P_min |

**轮休调度（热备用管理）**
- 跟踪每台风机的 `hotStandbyHours`（累计热备用时间）
- `NextStartId` / `NextStopId` 通过共享内存与外部调度系统交互

**演示验证点**
- [ ] t=0-30s：18MW正常跟踪
- [ ] t=60-120s：8MW深度限电，触发 DEEP_CURTAILMENT
- [ ] t=120-180s：备用调用+15MW
- [ ] 风机5安全指数S从1.0降至0.3 → 自动RESTRICTED

---

## 第12页 | 场景6：通信中断安全模式

| 属性 | 值 |
|------|-----|
| **策略名称** | Safety Mode Manager |
| **原始作者** | 章渲祺 + 李浚晞 |
| **规模** | 8台 × 5MW = 40MW |
| **核心算法** | 通信冻结 + 三级极端风况降载 + 协调恢复 |
| **KPI** | 1440min不间断，三级分级响应正确 |

**三子模式状态机**

```
           commHealthy==false
  NORMAL ────────────────────→ COMM_LOSS_FREEZE
    │                           功率冻结 = 当前功率
    │ extremeType≠NONE
    │ 或 WindSpeed>25m/s
    │ 或 Turbulence>0.25
    └───────────────────→ EXTREME_WIND_AUTONOMOUS
                             分级降载（见表）
```

**通信中断冻结**
- 触发瞬间：冻结当前全场功率 `frozenPower = farm.totalPowerMW`
- 冻结期间：每台风机维持冻结功率不变
- 恢复时：进入 NORMAL 子模式的恢复流程

**三级极端风况降载**

| 极端类型 | 触发条件 | 降载系数 | 剩余功率 | 响应 |
|----------|----------|----------|----------|------|
| CUT_OUT 切出风速 | WindSpeed > 25 m/s | ×0.70 | 30%降载 | 紧急停机保护 |
| HIGH_TURB 高湍流 | Turbulence > 0.25 | ×0.85 | 15%降载 | 快速降载 |
| STORM_RIDE 风暴穿越 | 其他极端条件 | ×0.75 | 25%降载 | 柔性降载 |

**协调恢复（NORMAL子模式）**
- 检测到通信恢复 + 风况正常
- 恢复爬坡率：2 MW/min（保证不会因快速恢复而导致二次冲击）
- 恢复目标：`recoveryTarget = cmd.targetPowerMW`（调度指令）或 `farm.totalPowerMW`（当前功率）
- 达到目标后清除 `frozen_` 标志，恢复正常运行

**演示验证点**
- [ ] 3:00-3:30 通信中断 → COMM_LOSS_FREEZE → 功率冻结
- [ ] 11:00-12:00 风暴穿越 → EXTREME_WIND_AUTONOMOUS → 25%降载
- [ ] 14:00-14:30 高湍流 → 15%降载
- [ ] 17:00-17:30 切出风速 → 30%降载+紧急停机
- [ ] 所有极端事件后 2MW/min 协调恢复

---

## 第13页 | HIL 自动化测试框架

**测试链路**
```
generator.py          runner.exe              unified_agc.exe
(7场景CSV生成)   →    (逐行注入SM)       →    (读取SM→AGC→写回SM)
                           ↓
                      runner.exe 采集输出
                           ↓
                      evaluate.py 评估
```

**7个测试场景**

| 场景 | CSV | 时长 | 步长 | 检测内容 |
|------|-----|------|------|----------|
| S1 | s1_baseline | 60s | 0.1s | 常规AGC稳态跟踪 |
| S2 | s2_wind_disturbance | 120s | 0.1s | 风速突变→SUPPRESS→恢复ECO |
| S3 | s3_freq_regulation | 90s | 0.1s | 49.7Hz扰动→调频→频率恢复 |
| S4 | s4_ramp_tracking | 60s | 0.1s | 15→25MW阶跃→爬坡跟踪 |
| S5 | s5_curtailment | 120s | 0.1s | 限电比例递增→模式切换 |
| S6 | s6_safety | 120s | 0.1s | 通信中断+极端天气安全响应 |
| S7 | s7_24h_combined | 86400s | 60s | 24小时全场景综合验证 |

**Runner 数据交互**

| 输入（Runner→SM→AGC） | 输出（AGC→SM→Runner采集） |
|------------------------|---------------------------|
| GRID.Frequency | WIND_AGC.TotalPower |
| WIND_AGC.WindSpeed | WIND_AGC.Setpoint |
| COMM.IsHealthy | WIND_AGC.Mode（场景编号） |
| EXTREME.SubType | GRID.Frequency（验证恢复） |
| CURTAIL.Ratio | |
| WIND_AGC.SchedulePower | |

**一键测试**
```bash
cd tools\data_generator
run_single.bat all     # 7场景自动批量测试
run_single.bat 3       # 单独测试场景3（一次调频）
```

---

## 第14页 | 编译与测试结果

**编译状态（VS2022 + CMake + MSVC v143）**

| # | 目标 | 类型 | 状态 |
|---|------|------|------|
| 1 | rt_db.lib | 静态库（27KB） | ✅ 0 error |
| 2 | rt_db_init.exe | 守护进程 | ✅ 0 error |
| 3 | test_rt_db.exe | 功能测试 | ✅ All passed |
| 4 | capacity_test.exe | 压力测试 | ✅ 性能达标 |
| 5 | unified_agc.exe | ★ 统一AGC（Daemon模式） | ✅ 0 error |
| 6-11 | scene1~6_*.exe | 6场景独立版本（回归对比） | ✅ 6/6 0 error |
| 12 | runner.exe | HIL Runner | ✅ 0 error |

**公共库单元测试：22/22 全部通过**

| 模块 | 测试项 | 结果 |
|------|--------|------|
| PID控制器 | P-only响应 / I累积 / 输出限幅 / 抗积分饱和 / setpoint接口 / 重置 | 6/6 ✅ |
| 限幅函数 | clamp上下界 / deadband / rampLimit正负向 / 不对称限速 | 8/8 ✅ |
| 风力功率曲线 | 切入/额定/切出边界 / 三次方模型 / 二次方模型 / 桨距效率 | 7/7 ✅ |
| 集成测试 | 30→70MW阶跃响应（0.8s收敛到死带内） | 1/1 ✅ |

**RT_DB 性能（capacity_test 实测）**
- 单点写入延迟 P99 < 2μs
- 写入吞吐 > 2000次/s，读取吞吐 > 100,000次/s
- 共享内存占用 ≈ 128KB

---

## 第15页 | 系统特点总结

**1. 工况自动识别，无需人工干预**
系统实时检测频率偏差、风速变化、通信状态、限电比例、调度指令，毫秒级自动切换策略。传统做法需要操作员手动判断+切换。

**2. 策略热切换，无缝衔接**
6个策略共享同一 `FarmStatus`，切换时无需重新初始化。上一策略的输出=下一策略的输入，功率零跳变。

**3. 统一数据空间，全场景一张表**
869个数据点覆盖全场所有变量，同一块共享内存。SCADA/HMI/数据分析只需连接这一个数据源。

**4. 算法与通信分离**
策略只负责控制计算，数据记录完全交给Logger层。新增场景只需写 `Strategy_*.h/.cpp`，不动数据通路。

**5. 架构可扩展，即插即用**
新场景 = 继承 StrategyBase + 实现 step() + 注册到策略数组 + 增加检测条件。工厂模式管理（`std::unique_ptr<StrategyBase>[]`）。

**6. 容错降级**
RT_DB不可用时自动进入Standalone模式，控制逻辑不受影响，零外部依赖。

**7. 可测试性，每层独立验证**
公共库22项单测 → RT_DB功能+压力测试 → HIL 7场景自动化闭环 → 独立场景exe回归对比。

**8. Daemon守护进程模式**
持续运行，外部信号驱动。相比旧版86秒硬编码脚本，更符合工业部署实际。

---

## 第16页 | 成果交付与后续规划

**已交付成果**

| 类别 | 内容 | 状态 |
|------|------|------|
| 代码 | 统一AGC系统（unified_agc/ 17文件）Daemon模式 | ✅ 编译运行通过 |
| 代码 | 公共基础库（common/ 5文件） | ✅ 22/22单测通过 |
| 代码 | 6场景原始代码归档 + V2 Logger改造 | ✅ 全部编译0 error |
| 代码 | HIL测试框架（tools/data_generator/） | ✅ 7场景自动测试 |
| 数据 | RT_DB 869数据点注册 | ✅ 守护进程可查 |
| 文档 | 项目计划（含进度跟踪） | ✅ |
| 文档 | 系统架构与代码讲解 | ✅ |
| 文档 | 数据点字典（869点完整定义） | ✅ |
| 文档 | 测试指南（五层测试方案 + HIL流程） | ✅ |
| 文档 | VS2022操作步骤（编译+测试全步骤） | ✅ |
| 文档 | 16页汇报PPT大纲（本文件） | ✅ |
| 测试 | 公共库单元测试 | ✅ 22/22 |
| 测试 | HIL自动化闭环测试 | ✅ 7场景全部触发 |
| 测试 | RT_DB功能+压力测试 | ✅ |

**后续规划**
- GUI实时监控面板（功率曲线/频率仪表盘/场景状态指示）
- 故障注入测试（通信中断+频率扰动同时发生等极端组合场景）
- 多场景并行联合仿真（多场景同时运行+共享数据空间）
- OPC UA/Modbus 工业协议适配
- 中煤方现场部署与联调