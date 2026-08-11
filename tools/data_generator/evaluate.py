#!/usr/bin/env python3
"""风电AGC HIL评估器 — 读Runner输出CSV, 评估+绘图"""
import argparse, csv, os, sys

try:
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    HAS_MPL = True
except ImportError:
    HAS_MPL = False

def load(path):
    rows = []
    with open(path, encoding='utf-8') as f:
        for r in csv.DictReader(f):
            rows.append({k: float(v) for k, v in r.items()})
    return rows

def evaluate(rows, scene_id):
    scenes = [r['active_scene'] for r in rows[30:]]  # skip startup
    errors = [r['error_pct'] for r in rows[30:]]
    powers = [r['actual_power_mw'] for r in rows]

    scene_set = set(int(s) for s in scenes)
    scene_count = {}
    for s in scenes: scene_count[int(s)] = scene_count.get(int(s), 0) + 1

    print(f'\n=== S{scene_id} Evaluation ===')
    print(f'  Scenes visited: {sorted(scene_set)}')
    for k, v in sorted(scene_count.items()):
        print(f'    Scene {k}: {v}/{len(scenes)} ({100*v/len(scenes):.1f}%)')
    print(f'  Avg tracking error: {sum(errors)/len(errors):.2f}%')
    print(f'  Max tracking error: {max(errors):.2f}%')
    print(f'  Power range: [{min(powers):.1f}, {max(powers):.1f}] MW')

    # Assertions
    passed, failed = 0, 0
    def check(cond, label, critical=True):
        nonlocal passed, failed
        tag = 'CRITICAL' if critical else 'WARN'
        if cond:
            print(f'  [PASS] {label}')
            passed += 1
        else:
            print(f'  [FAIL] ({tag}) {label}')
            failed += 1

    check(max(errors) < 100, f'Max error {max(errors):.1f}% < 100%')
    check(len(scene_set) >= 1, f'{len(scene_set)} scene(s) detected')

    if scene_id == '1': check(1 in scene_set, 'Scene 1 visited (baseline)')
    if scene_id == '2': check(2 in scene_set, 'Scene 2 visited (wind disturb)')
    if scene_id == '3': check(3 in scene_set, 'Scene 3 visited (freq reg)')
    if scene_id == '4': check(4 in scene_set, 'Scene 4 visited (ramp track)')
    if scene_id == '5': check(5 in scene_set, 'Scene 5 visited (curtail)')
    if scene_id == '6': check(6 in scene_set, 'Scene 6 visited (safety)')

    print(f'\n  Total: {passed} PASS, {failed} FAIL')
    return passed, failed

def plot(rows, scene_id, out_png):
    if not HAS_MPL:
        print('[SKIP] matplotlib not installed. pip install matplotlib')
        return

    ts   = [r['timestamp_s'] for r in rows]
    pwr  = [r['actual_power_mw'] for r in rows]
    sp   = [r['setpoint_mw'] for r in rows]
    sc   = [r['active_scene'] for r in rows]
    err  = [r['error_pct'] for r in rows]
    freq = [r['freq_hz'] for r in rows]

    fig, (ax1, ax2, ax3) = plt.subplots(3, 1, figsize=(14, 10), sharex=True)
    fig.suptitle(f'Wind AGC S{scene_id} HIL Test Results', fontsize=14, fontweight='bold')

    ax1.plot(ts, pwr, 'r-', label='Actual Power', linewidth=1)
    ax1.plot(ts, sp,  'b--', label='Setpoint', linewidth=1)
    ax1.set_ylabel('Power (MW)')
    ax1.legend(loc='upper right')
    ax1.grid(True, alpha=0.3)

    ax2.plot(ts, sc, 'g-', label='Active Scene', linewidth=1)
    ax2.set_ylabel('Scene #')
    ax2.set_yticks([1,2,3,4,5,6])
    ax2.legend(loc='upper right')
    ax2.grid(True, alpha=0.3)

    ax3.plot(ts, freq, 'purple', label='Grid Freq', linewidth=0.8)
    ax3.axhline(50.0, color='gray', linestyle='--', alpha=0.5)
    ax3.set_ylabel('Frequency (Hz)')
    ax3.set_xlabel('Time (s)')
    ax3.legend(loc='upper right')
    ax3.grid(True, alpha=0.3)

    plt.tight_layout()
    plt.savefig(out_png, dpi=120)
    print(f'[PLOT] Saved -> {out_png}')

def main():
    p = argparse.ArgumentParser(description='Wind AGC HIL Evaluator')
    p.add_argument('--result', '-r', required=True, help='Runner output CSV')
    p.add_argument('--scene', '-s', default='?', help='Scene ID for labeling')
    p.add_argument('--no-plot', action='store_true')
    args = p.parse_args()

    rows = load(args.result)
    evaluate(rows, args.scene)

    if not args.no_plot:
        png = os.path.splitext(args.result)[0] + '.png'
        plot(rows, args.scene, png)

if __name__ == '__main__':
    main()