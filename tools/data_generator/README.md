# 风电AGC数据生成器 + HIL测试框架

---

## 文件说明

```
tools/data_generator/
├── generator.py      # 数据生成器: 生成6场景+24h综合工况CSV
├── runner.cpp         # HIL Runner: 读CSV→注入共享内存→采集AGC响应
├── evaluate.py        # 评估器: 读Runner输出→断言评估→绘图
├── build_runner.bat   # 编译Runner (Developer Command Prompt)
├── run_single.bat     # 单场景一键测试
├── README.md          # 本文件
└── output/            # 生成的数据 + 结果
```

---

## 完整使用流程

### 第一步：编译Runner（仅需一次）

打开 **Developer Command Prompt for VS 2022**：

```bash
cd /d c:\Users\32567\Desktop\实验室\风电AGC\tools\data_generator
build_runner.bat
```

输出 `runner.exe`。

### 第二步：启动共享内存（窗口A，保持运行）

```bash
cd /d c:\Users\32567\Desktop\实验室\风电AGC
build\bin\Release\rt_db_init.exe
```

### 第三步：生成测试数据（窗口B）

打开普通 **cmd**：

```bash
cd /d c:\Users\32567\Desktop\实验室\风电AGC\tools\data_generator
python generator.py --scene 1
```

### 第四步：运行注入+采集（窗口B）

```bash
runner.exe output\s1_baseline.csv output\s1_result.csv
```

Runner会逐行读取CSV，把频率、风速、调度指令写入共享内存，同时采集AGC的功率、场景号输出。

### 第五步：评估+绘图（窗口B）

```bash
python evaluate.py -r output\s1_result.csv -s 1
```

生成 `s1_result.png` 图表（功率曲线+场景切换+频率曲线）。

### 一键运行（替代第三~五步）

```bash
# 前提: 守护进程已启动
run_single.bat 1    # 测试S1
run_single.bat 4    # 测试S4
run_single.bat all  # 全部场景
```

---

## 生成器命令

```bash
python generator.py --scene all          # 全部场景
python generator.py --scene 1            # S1 稳态 60s
python generator.py --scene 7 --step 60  # 24h综合 1min步长
python generator.py --scene all --seed 999  # 换噪声种子
```

## 生成文件

| 文件 | 场景 | 时长 | 行数 |
|------|------|------|------|
| s1_baseline.csv | 稳态跟踪 | 60s | 601 |
| s2_wind_disturbance.csv | 风速波动 | 120s | 1201 |
| s3_freq_regulation.csv | 一次调频 | 90s | 901 |
| s4_ramp_tracking.csv | 爬坡跟踪 | 60s | 601 |
| s5_curtailment.csv | 限电管理 | 120s | 1201 |
| s6_safety.csv | 安全模式 | 120s | 1201 |
| s7_24h_combined.csv | 24h综合 | 86400s | 864001 |

## Runner输出CSV

| 列 | 含义 |
|----|------|
| timestamp_s | 仿真时间 |
| actual_power_mw | AGC输出的实际功率 |
| setpoint_mw | AGC计算的功率指令 |
| active_scene | 当前场景号(1-6) |
| freq_hz | 电网频率 |
| error_pct | 跟踪误差(%) |