# Wind Farm AGC — 风电场自动发电控制系统

多场景统一架构 · 智能工况切换 · 共享内存实时监控 · HIL 自动化测试

---

## 项目简介

本项目将 6 位同学独立开发的 6 个风电 AGC 仿真程序整合为**一套统一架构**：

- **统一类型系统**：合并 6 套互不兼容的类型定义，全系统一套 `CommonTypes.h`
- **统一策略框架**：6 个场景各实现 `StrategyBase` 接口，`ConditionDetector` 7 级优先级自动切换
- **统一数据总线**：基于共享内存的 RT_DB，869 个数据点，微秒级延迟
- **Daemon 守护进程**：持续运行，外部信号驱动，接近真实工业部署
- **HIL 自动化测试**：`generator.py → runner.exe → unified_agc.exe → evaluate.py` 全链路闭环

```
┌──────────────────────────────────────────┐
│  HIL 测试框架 (tools/data_generator/)     │
│  自动化闭环: generator → runner → AGC    │
├──────────────────────────────────────────┤
│  统一 AGC 层 (unified_agc/)  17 文件     │
│  Daemon: ConditionDetector → 6 Strategy  │
├──────────────────────────────────────────┤
│  公共基础层 (common/)  5 文件            │
│  CommonTypes + PID + Limiter + WindCurve │
├──────────────────────────────────────────┤
│  共享内存层 (RT_DB)  869 数据点          │
│  seqlock 无锁读写 + SPSC 环形队列       │
├──────────────────────────────────────────┤
│  6 场景策略层 (archive/scenes/)          │
│  原始算法全部保留，零改动                 │
└──────────────────────────────────────────┘
```

---

## 目录结构

```
风电AGC/
│
├── unified_agc/                  ★ 核心交付物 — 统一 AGC 系统
│   ├── main.cpp                  Daemon 守护进程入口
│   ├── UnifiedAGC.h / .cpp       统一控制器（检测→路由→执行→日志）
│   ├── ConditionDetector.h       工况检测器（7 级优先级决策树）
│   ├── StrategyBase.h            策略基类（统一接口）
│   ├── Strategy_Normal.h/.cpp    场景 1：常规 AGC + 经济调度
│   ├── Strategy_Disturbance.h/.cpp 场景 2：风速波动抑制
│   ├── Strategy_FrequencyReg.h/.cpp 场景 3：一次调频
│   ├── Strategy_RampTracking.h/.cpp 场景 4：爬坡跟踪
│   ├── Strategy_Curtailment.h/.cpp  场景 5：限电备用
│   ├── Strategy_Safety.h/.cpp    场景 6：安全模式
│   └── UnifiedLogger.h           共享内存输出（16 个数据点）
│
├── common/                       公共基础库（660 行，22/22 单测通过）
│   ├── CommonTypes.h             统一类型定义（10 枚举 + 6 结构体）
│   ├── PIDController.h           通用 PID（抗积分饱和 + 微分滤波）
│   ├── Limiter.h                 限幅 / 死区 / 斜坡限制
│   ├── WindPowerCurve.h          风力功率曲线模型（3 种）
│   └── RtDbLoggerBase.h          共享内存 Logger 基类
│
├── archive/scenes/               原始场景代码归档（算法全部保留）
│   ├── scene1_baseline/          场景 1：常规 AGC 经济调度（王翰铭，14 文件）
│   ├── scene2_wind_disturbance/  场景 2：风速扰动平抑（卓世杰，10 文件）
│   ├── scene3_freq_regulation/   场景 3：一次调频（郑怀勇，11 文件）
│   ├── scene4_ramp_tracking/     场景 4：爬坡跟踪（李隆就，15 文件）
│   ├── scene5_curtailment/       场景 5：限电备用（赖嘉雯，28 文件）
│   └── scene6_safety/            场景 6：安全模式（章渲祺+李浚晞，12 文件）
│
├── tools/data_generator/         HIL 自动化测试框架
│   ├── generator.py              测试数据生成器（7 个场景，CSV 输出）
│   ├── runner.cpp                HIL Runner（CSV → 共享内存注入 → 采集）
│   ├── evaluate.py               结果评估脚本（误差分析 + 场景切换验证）
│   ├── run_single.bat           一键测试（单场景 / 全部 7 场景）
│   ├── build_runner.bat          Runner 编译脚本
│   └── output/                   测试输出目录（*_result.csv）
│
├── tools/rt_db_ref/              RT_DB 源码的目录 Junction（符号链接）
│
├── docs/                         文档
│   ├── 测试指南.md                五层测试方案 + HIL 流程 + 验收清单
│   ├── VS2022操作步骤.md          从零编译到运行的全步骤
│   ├── 系统介绍与代码讲解.md      甲方汇报用 — 架构 + 代码详解
│   ├── 项目计划-风电场AGC多场景统一架构改造.md  分阶段实施 + 进度
│   ├── 数据点字典.md              869 个共享内存数据点完整定义
│   └── 汇报PPT大纲-详细版.md      16 页 PPT 大纲（含各场景详解）
│
├── test/                         测试
│   └── test_common.cpp           公共库单元测试（22/22 通过）
│
├── references/                   参考资料
│   ├── 智能纪要：新能源控制项目研发规划 2026年7月14日.docx
│   ├── 风电场AGC多场景统一架构改造与系统集成.pptx
│   └── 风电场AGC自动发电控制系统设计与多场景仿真验证.docx
│
├── 储能协调控制器实时数据库与缓存模块详细设计/  RT_DB 共享内存库源码
│   └── 储能协调控制器实时数据库与缓存模块详细设计/
│       ├── include/rt_db_api.h   C API 头文件
│       ├── src/rt_db/            核心实现（seqlock + 原子操作）
│       ├── src/tools/init_rt_db.c 守护进程（869 点注册 + 监控面板）
│       └── test/                 功能测试 + 压力测试
│
├── CMakeLists.txt                顶层 CMake 配置（VS2022 + MSVC）
├── build_all.bat                 一键编译脚本
├── setup.ps1                     首次设置脚本（克隆后运行一次，PowerShell）
├── README.md                     本文件
└── .gitignore                    Git 忽略规则
```

---

## 环境要求

| 依赖 | 版本 | 用途 |
|------|------|------|
| **Windows** | 10 / 11 | 运行平台（共享内存依赖 Windows FileMapping） |
| **Visual Studio 2022** | 17.x | C++ 编译器（MSVC v143） |
| **CMake** | ≥ 3.20 | 构建系统（VS2022 自带或单独安装） |
| **Python** | ≥ 3.8 | 测试数据生成 + 结果评估 |
| **Git Bash** | 任意版本 | 运行 shell 命令（可选） |

VS2022 安装时必须勾选 **"使用 C++ 的桌面开发"** 工作负载（含 MSVC 和 CMake 工具）。

---

## 快速开始

### 0. 克隆后首次设置（必须，仅一次）

打开 **PowerShell**（非 cmd），在项目根目录执行：

```powershell
powershell -ExecutionPolicy Bypass -File setup.ps1
```

这一步会创建 `tools/rt_db_ref` 目录链接（用于绕过 Windows 批处理文件的中文路径编码问题）并检查 Python、CMake 等依赖是否就绪。

### 1. 编译

打开 **Developer Command Prompt for VS 2022**（开始菜单搜索 "Developer"）：

```bash
cd /d <项目目录>
build_all.bat
```

编译产物在 `build\bin\Release\`：

| 文件 | 说明 |
|------|------|
| `rt_db_init.exe` | RT_DB 共享内存守护进程 |
| `unified_agc.exe` | ★ 统一 AGC 系统（Daemon 模式） |
| `test_rt_db.exe` | RT_DB 功能测试 |
| `capacity_test.exe` | RT_DB 压力测试 |
| `scene1~6_*.exe` | 6 个原始独立场景（回归对比用） |

### 3. 运行公共库单元测试（可选，无需 VS2022）

```bash
g++ -std=c++17 -I./common test/test_common.cpp -o test_common.exe
./test_common.exe
# 预期: 22/22 PASSED
```

### 4. 启动 AGC 系统

**终端 A** — 启动共享内存：

```bash
cd /d <项目目录>
build\bin\Release\rt_db_init.exe
```

看到 `Pre-registered 881 total named data points` 即成功。保持运行。

**终端 B** — 启动 AGC Daemon：

```bash
cd /d <项目目录>
build\bin\Release\unified_agc.exe
```

确认输出 `[OUT] Logger connected` 和 `[IN] Input reader connected`。

### 5. 运行 HIL 自动化测试

**终端 C**：

```bash
cd /d <项目目录>\tools\data_generator

# 编译 Runner（首次需要）
build_runner.bat

# 测试单个场景
run_single.bat 3

# 测试全部 7 个场景
run_single.bat all
```

测试结果在 `tools\data_generator\output\*_result.csv`。

---

## 六场景一览

| # | 场景 | 原始作者 | 规模 | 触发条件 | KPI |
|---|------|---------|------|----------|-----|
| 1 | 常规 AGC 经济调度 | 王翰铭 | 100台×3MW=300MW | 默认模式 | RMSE≤2%, 合格率≥95% |
| 2 | 风速波动抑制 | 卓世杰 | 4台×2.5MW=10MW | 风速突变 >1.5m/s | 波动抑制 >5% |
| 3 | 一次调频 | 郑怀勇 | 10台×2.5MW=25MW | 频率偏差 >0.10Hz | <30s 恢复 50Hz |
| 4 | 爬坡跟踪 | 李隆就 | 2台×50MW=100MW | STEP/RAMP, ΔP>5MW | 跟踪误差 <0.5MW |
| 5 | 限电备用 | 赖嘉雯 | 50台×2MW=100MW | 限电比例 >40% | 三模式自动切换 |
| 6 | 安全模式 | 章渲祺+李浚晞 | 8台×5MW=40MW | 通信中断/极端风况 | 三级分级降载响应 |

**优先级**: 安全(6) > 一次调频(3) > 波动抑制(2) > 限电(5) > 爬坡(4) > 常规(1)

---

## HIL 测试管线

```
generator.py  ──→  runner.exe  ──→  unified_agc.exe  ──→  evaluate.py
 (生成 CSV)        (注入+采集)       (AGC 计算响应)         (评估结果)
```

### 测试场景覆盖

| 场景 | CSV 文件 | 时长 | 步长 | 测试内容 |
|------|----------|------|------|----------|
| S1 | s1_baseline | 60s | 0.1s | 常规 AGC 稳态跟踪 |
| S2 | s2_wind_disturbance | 120s | 0.1s | 风速突变 → SUPPRESS → 恢复 |
| S3 | s3_freq_regulation | 90s | 0.1s | 49.7Hz 扰动 → 调频 → 频率恢复 |
| S4 | s4_ramp_tracking | 60s | 0.1s | 调度阶跃 → 爬坡跟踪 |
| S5 | s5_curtailment | 120s | 0.1s | 限电递增 → 深度限电切换 |
| S6 | s6_safety | 120s | 0.1s | 通信中断 + 极端天气安全响应 |
| S7 | s7_24h_combined | 86400s | 60s | 24 小时全场景综合验证 |

### Runner 数据交互

| 输入（Runner → SM → AGC） | 输出（AGC → SM → Runner 采集） |
|---------------------------|-------------------------------|
| `GRID.Frequency` | `WIND_AGC.TotalPower` |
| `WIND_AGC.WindSpeed` | `WIND_AGC.Setpoint` |
| `COMM.IsHealthy` | `WIND_AGC.Mode`（场景编号 1-6） |
| `EXTREME.SubType` | `GRID.Frequency`（验证恢复过程） |
| `CURTAIL.Ratio` | |
| `WIND_AGC.SchedulePower` | |

---

## 关键指标

| 指标 | 数值 | 验证方式 |
|------|------|----------|
| 公共库单元测试 | 22/22 通过 | `test_common.cpp` |
| 编译目标 | 12/12（0 error） | `build_all.bat` |
| RT_DB 数据点 | 869 个 | `rt_db_init.exe` 启动日志 |
| 共享内存写入延迟 | P99 < 2μs | `capacity_test.exe` |
| 写入吞吐 | > 2000 次/秒 | `capacity_test.exe` |
| 读取吞吐 | > 100,000 次/秒 | `capacity_test.exe` |
| 共享内存占用 | ≈ 128KB | 任务管理器 |
| HIL 测试场景 | 7/7 全部通过 | `run_single.bat all` |

---

## 文档索引

| 文档 | 说明 | 适合 |
|------|------|------|
| [测试指南](docs/测试指南.md) | 五层测试方案 + HIL 流程 + 验收清单 | 开发/测试 |
| [VS2022操作步骤](docs/VS2022操作步骤.md) | 从零编译到运行的全步骤 | 新人上手 |
| [系统介绍与代码讲解](docs/系统介绍与代码讲解.md) | 架构设计 + 核心代码逐文件讲解 | 甲方汇报 / 新人理解 |
| [数据点字典](docs/数据点字典.md) | 869 个共享内存数据点完整定义 | 联调 / 扩展 |
| [项目计划](docs/项目计划-风电场AGC多场景统一架构改造.md) | 分阶段实施 + 进度跟踪 | 项目管理 |
| [PPT大纲](docs/汇报PPT大纲-详细版.md) | 16 页汇报 PPT 大纲（含各场景详解） | 成果汇报 |

---

## 开发指南（给组员）

### 添加新场景

1. 在 `unified_agc/` 下创建 `Strategy_NewScene.h` 和 `Strategy_NewScene.cpp`
2. 继承 `StrategyBase`，实现 `name()`、`sceneId()`、`step()`
3. 在 `UnifiedAGC.cpp` 构造函数中注册：`strategies_[N] = std::make_unique<StrategyNewScene>()`
4. 在 `ConditionDetector.h` 中添加检测条件
5. 在 `UnifiedLogger.h` 中添加新场景需要输出的数据点（如需要）
6. 在 `tools/data_generator/generator.py` 中添加测试数据生成函数
7. 运行 `build_all.bat` 编译，`run_single.bat all` 验证

### 修改策略算法

- 策略实现在 `unified_agc/Strategy_*.cpp`，直接修改即可
- 公共算法（PID、滤波器等）在 `common/` 下，修改后运行 `test_common.cpp` 验证
- 修改后必须运行 `run_single.bat all` 确保其他场景不被影响

### 代码风格

- C++17 标准
- 命名空间 `unified::`（策略）、`common::`（公共库）
- UTF-8 编码，MSVC `/utf-8` 编译选项
- 批处理文件使用纯 ASCII，不含中文字符（避免 GBK 编码问题）

### 常见问题

| 问题 | 解决 |
|------|------|
| `unified_agc.exe` 提示 `[IN] Input reader not connected` | 先启动 `rt_db_init.exe`，再启动 `unified_agc.exe` |
| `runner.exe` 无法连接共享内存 | 确保 `rt_db_init.exe` 和 `unified_agc.exe` 都在运行 |
| 编译报 `rt_db_api.h` 找不到 | 确认 `储能协调控制器实时数据库与缓存模块详细设计/` 目录完整 |
| 批处理文件中文乱码 | 所有 `.bat` 文件已改为纯 ASCII，用 UTF-8 保存即可 |
| 共享内存未释放（`already exists`） | 重启电脑，或找到之前的 `rt_db_init.exe` 窗口按 `Ctrl+C` |
| `run_single.bat` 报 `python` 找不到 | 安装 Python 3.x 并添加到系统 PATH |

---

## 许可证

本项目为学术研发项目，版权归属于项目组成员及指导老师。