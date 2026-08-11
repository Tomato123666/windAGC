/**
 * @file test_common.cpp
 * @brief 公共基础库单元测试 —— 不依赖RT_DB共享内存
 *
 * 编译: g++ -std=c++17 -I../common test_common.cpp -o test_common
 * 运行: ./test_common
 */
#include <iostream>
#include <cassert>
#include <cmath>
#include <vector>

// 引入公共库
#include "PIDController.h"
#include "Limiter.h"
#include "WindPowerCurve.h"

// ============================================================================
// 辅助宏
// ============================================================================
static int g_passed = 0, g_failed = 0;

#define TEST(name)  std::cout << "  [" << name << "] "
#define PASS()      do { std::cout << "PASSED\n"; g_passed++; } while(0)
#define FAIL(msg)   do { std::cout << "FAILED: " << msg << "\n"; g_failed++; } while(0)
#define CHECK(cond) do { if (cond) PASS(); else FAIL(#cond); } while(0)
#define CHECK_NEAR(a, b, eps) do { \
    if (std::abs((a)-(b)) <= (eps)) PASS(); \
    else { std::cout << "FAILED: |" << (a) << " - " << (b) << "| = " << std::abs((a)-(b)) << " > " << (eps) << "\n"; g_failed++; } \
} while(0)

// ============================================================================
// 1. PID控制器测试
// ============================================================================
void test_pid_basic() {
    std::cout << "\n=== PID Controller Tests ===\n";

    // P-only controller
    TEST("P-only: error=2.0 → output=Kp*error"); {
        common::PIDController pid(0.5, 0.0, 0.0, 1.0, 10.0, -10.0, 10.0);
        double out = pid.compute(2.0);
        CHECK_NEAR(out, 1.0, 0.001);
    }

    // PI controller: integral accumulation
    TEST("PI: integral accumulates over 10 steps"); {
        common::PIDController pid(0.0, 0.1, 0.0, 1.0, 100.0, -100.0, 100.0);
        double sum = 0.0;
        for (int i = 0; i < 10; i++) sum += pid.compute(1.0);
        // Integral: each step adds 0.1*1.0*1.0 = 0.1, 10 steps = 1.0
        CHECK_NEAR(pid.getIntegral(), 1.0, 0.01);
    }

    // Anti-windup: output clamped
    TEST("Anti-windup: output clamped to [-5, 5]"); {
        common::PIDController pid(10.0, 0.0, 0.0, 1.0, 10.0, -5.0, 5.0);
        double out = pid.compute(2.0);  // 10*2 = 20, clamped to 5
        CHECK_NEAR(out, 5.0, 0.001);
    }

    // Conditional integration: don't integrate when saturated in same direction
    TEST("Conditional integration: big error doesn't keep winding up"); {
        common::PIDController pid(0.0, 1.0, 0.0, 1.0, 10.0, -5.0, 5.0);
        pid.compute(10.0);  // first call: accumulates integral, then saturates
        double afterFirst = pid.getIntegral();
        pid.compute(10.0);  // still positive error, should NOT integrate further
        double afterSecond = pid.getIntegral();
        // Integral should NOT have increased on the second call (anti-windup blocks it)
        CHECK_NEAR(afterFirst, afterSecond, 0.001);
    }

    // Reset
    TEST("Reset: zeros integral and state"); {
        common::PIDController pid(1.0, 1.0, 1.0, 1.0);
        pid.compute(5.0); pid.compute(5.0);
        pid.reset();
        CHECK_NEAR(pid.getIntegral(), 0.0, 0.001);
    }

    // setpoint-measurement convenience
    TEST("setpoint-measurement: compute(setpoint, measurement)"); {
        common::PIDController pid(1.0, 0.0, 0.0, 1.0, 10.0, -10.0, 10.0);
        double out = pid.compute(80.0, 70.0);  // error=10
        CHECK_NEAR(out, 10.0, 0.001);
    }
}

// ============================================================================
// 2. 限幅函数测试
// ============================================================================
void test_limiter() {
    std::cout << "\n=== Limiter Tests ===\n";

    TEST("clamp: value in range unchanged"); {
        CHECK_NEAR(common::clamp(5.0, 0.0, 10.0), 5.0, 0.001);
    }

    TEST("clamp: value below min → min"); {
        CHECK_NEAR(common::clamp(-5.0, 0.0, 10.0), 0.0, 0.001);
    }

    TEST("clamp: value above max → max"); {
        CHECK_NEAR(common::clamp(15.0, 0.0, 10.0), 10.0, 0.001);
    }

    TEST("deadband: |value| < band → 0"); {
        CHECK_NEAR(common::deadband(0.003, 0.005), 0.0, 0.001);
    }

    TEST("deadband: |value| > band → unchanged"); {
        CHECK_NEAR(common::deadband(0.1, 0.005), 0.1, 0.001);
    }

    TEST("rampLimit: within rate → target"); {
        double result = common::rampLimit(0.0, 5.0, 10.0, 1.0);  // max 10/sec, need 5
        CHECK_NEAR(result, 5.0, 0.001);
    }

    TEST("rampLimit: exceeds rate → limited"); {
        double result = common::rampLimit(0.0, 100.0, 10.0, 1.0);  // max 10/sec, need 100
        CHECK_NEAR(result, 10.0, 0.001);
    }

    TEST("rampLimit: negative direction"); {
        double result = common::rampLimit(0.0, -100.0, 10.0, 1.0);
        CHECK_NEAR(result, -10.0, 0.001);
    }
}

// ============================================================================
// 3. 风力功率曲线测试
// ============================================================================
void test_wind_power_curve() {
    std::cout << "\n=== Wind Power Curve Tests ===\n";

    common::WindPowerCurve curve(2.5, 3.0, 12.0, 25.0, common::PowerCurveModel::CUBIC);

    TEST("Below cut-in (2 m/s) → 0"); {
        CHECK_NEAR(curve.getPower(2.0), 0.0, 0.001);
    }

    TEST("At rated wind (12 m/s) → rated power"); {
        CHECK_NEAR(curve.getPower(12.0), 2.5, 0.01);
    }

    TEST("Above rated, below cut-out (18 m/s) → rated power"); {
        CHECK_NEAR(curve.getPower(18.0), 2.5, 0.01);
    }

    TEST("Above cut-out (26 m/s) → 0"); {
        CHECK_NEAR(curve.getPower(26.0), 0.0, 0.001);
    }

    TEST("Half rated wind (7.5 m/s) → cubic scaling"); {
        double p = curve.getPower(7.5);
        double expected = 2.5 * std::pow(7.5/12.0, 3.0);
        CHECK_NEAR(p, expected, 0.01);
    }

    // Pitch efficiency loss
    TEST("Pitch efficiency: 4° pitch → ~11% loss"); {
        double p = curve.getActualPower(12.0, 4.0, 8.0, 0.22);
        // eff = 1 - (4/8)*0.22 = 1 - 0.11 = 0.89
        CHECK_NEAR(p, 2.5 * 0.89, 0.05);
    }

    // Model switching
    TEST("Quadratic model: check"); {
        common::WindPowerCurve qcurve(2.5, 3.0, 12.0, 25.0, common::PowerCurveModel::QUADRATIC);
        double ratio = (7.5 - 3.0) / (12.0 - 3.0);  // = 0.5
        double expected = 2.5 * ratio * ratio;  // = 0.625
        CHECK_NEAR(qcurve.getPower(7.5), expected, 0.01);
    }
}

// ============================================================================
// 4. PID + Limiter 集成测试 (模拟场景4爬坡跟踪)
// ============================================================================
void test_integration_ramp_tracking() {
    std::cout << "\n=== Integration: Ramp Tracking Simulation ===\n";

    // 配置场景4参数: Kp=0.3, deadband=0.5MW
    common::PIDController pid(0.3, 0.0, 0.0, 0.1, 10.0, -5.0, 5.0);
    double actual = 30.0;   // 当前功率 30MW
    double target = 70.0;   // 目标功率 70MW (阶跃)
    double dt = 0.1;        // 100ms控制周期

    TEST("Step response: reaches target within deadband"); {
        int steps = 0;
        for (int i = 0; i < 600; i++) {  // 最多60s
            double error = target - actual;
            double comp = pid.compute(error);
            double desired = target + comp;
            desired = common::clamp(desired, 0.0, 100.0);
            // 一阶惯性
            actual = common::rampLimit(actual, desired, 50.0, dt);  // 模拟50MW/s响应
            steps++;
            if (std::abs(target - actual) < 0.5) break;
        }
        std::cout << "  Converged in " << steps * dt << "s (actual=" << actual << " MW) → ";
        CHECK(steps < 600 && std::abs(actual - target) < 0.5);
    }
}

// ============================================================================
int main() {
    std::cout << "╔══════════════════════════════════════════╗\n";
    std::cout << "║  风电场AGC 公共基础库 单元测试           ║\n";
    std::cout << "╚══════════════════════════════════════════╝\n";

    test_pid_basic();
    test_limiter();
    test_wind_power_curve();
    test_integration_ramp_tracking();

    std::cout << "\n===========================================\n";
    std::cout << "  TOTAL: " << (g_passed + g_failed) << " tests, "
              << g_passed << " PASSED, " << g_failed << " FAILED\n";
    std::cout << "===========================================\n";

    return g_failed > 0 ? 1 : 0;
}
