#!/usr/bin/env python3
"""
Wind-AGC Web Server — Flask + SocketIO
Adapted from PV-ESS-AGC's agc_ui_server.py
Serves the Vue.js dashboard on port 5189, bridges SHM data to web clients
"""
import os
import sys
import time
import json
import threading
import logging
from datetime import datetime
from flask import Flask, jsonify, request, send_from_directory
from flask_cors import CORS
from flask_socketio import SocketIO, emit

# ============================================================
# App Setup
# ============================================================
BASE_DIR = os.path.dirname(os.path.abspath(__file__))           # tools/wind_agc_ui/
PROJECT_DIR = os.path.dirname(os.path.dirname(BASE_DIR))        # windAGC-main/

app = Flask(__name__, static_folder=BASE_DIR, static_url_path='')
app.config['SECRET_KEY'] = 'wind-agc-secret-2026'
CORS(app, resources={r"/*": {"origins": "*"}})
socketio = SocketIO(app, cors_allowed_origins="*", async_mode='threading', ping_timeout=10, ping_interval=5)

log = logging.getLogger('wind_agc_server')
log.setLevel(logging.INFO)
if not log.handlers:
    h = logging.StreamHandler()
    h.setFormatter(logging.Formatter('[%(asctime)s] %(levelname)s %(message)s', datefmt='%H:%M:%S'))
    log.addHandler(h)

# ============================================================
# SHM Bridge (lazy import)
# ============================================================
shm = None
shm_lock = threading.Lock()

shm_init_tried = False  # Only try SHM init once

def get_shm():
    """Lazy-init SHM bridge (one-shot: only try once)."""
    global shm, shm_init_tried
    if shm is not None:
        return shm
    if shm_init_tried:
        return None
    shm_init_tried = True
    try:
        from agc_shm_bridge import AgcShmReader
        shm = AgcShmReader()
        if not shm.attach():
            log.warning("SHM not available - running in simulation-only mode")
        else:
            log.info("SHM connected - real-time data active")
    except Exception as e:
        log.warning(f"SHM init failed: {e}")
    return shm

# ============================================================
# Simulation State (fallback when SHM unavailable)
# ============================================================
SIM = {
    'frequency': 50.00, 'voltage': 1.0, 'totalPower': 0.0,
    'schedulePower': 15.0, 'windSpeed': 8.0, 'commHealthy': True,
    'sceneActive': 1, 'sceneName': 'S1 稳态', 'sceneDesc': '稳态计划跟踪',
    'mode': 'Auto', 'emergency': False,
    'turbines': {},
    'avgRpm': 12.0, 'avgPitch': 0.0,
    'mae': 0.0, 'rmse': 0.0,
    'cycle': 0, 'heartbeat': 0,
    's6Strategy': 1, 'curtailRatio': 0.0,
    'extremeType': 0, 'rocof': 0.0,
    'planBaseline': 15.0, 'powerSetpoint': 0.0,
}

# Init 10 turbines
for i in range(1, 11):
    SIM['turbines'][f'WT_{i:02d}'] = {
        'id': f'WT_{i:02d}', 'powerMW': 0.0, 'windSpeedMs': 8.0,
        'rotorSpeedRPM': 12.0, 'pitchAngleDeg': 0.0,
        'state': 'NORMAL', 'role': 'BASE', 'avail': True,
        'targetMW': 0.0, 'safetyIndex': 1.0,
    }

# History buffer (last 300 cycles = 5 min)
HISTORY = []
MAX_HIST = 300

# Console log buffer
CONSOLE_LOG = []
MAX_CONSOLE = 200

# Alerts
ALERTS = []
MAX_ALERTS = 50

# ============================================================
# Background: Read SHM / Update SIM
# ============================================================
def bg_read_loop():
    """Background thread: read SHM every 300ms, emit via SocketIO."""
    global SIM
    last_hb_write = 0

    while True:
        try:
            s = get_shm()
            if s and s.connected:
                # Read all SHM data (flat points map)
                data = s.read_all()
                pts = data.get('points') or {}

                SIM['frequency'] = pts.get('GRID.Frequency', SIM['frequency'])
                SIM['voltage'] = pts.get('GRID.Voltage', SIM['voltage'])
                SIM['schedulePower'] = pts.get('WIND_AGC.SchedulePower', SIM['schedulePower'])
                SIM['windSpeed'] = pts.get('WIND_AGC.WindSpeed', SIM['windSpeed'])
                SIM['totalPower'] = pts.get('WIND_AGC.TotalPower', SIM['totalPower'])
                SIM['powerSetpoint'] = pts.get('WIND_AGC.Setpoint', SIM['powerSetpoint'])
                SIM['sceneActive'] = int(pts.get('WIND_AGC.Mode', SIM['sceneActive']))
                SIM['s6Strategy'] = int(pts.get('SAFETY.CurrentMode', SIM['s6Strategy']))
                SIM['commHealthy'] = pts.get('COMM.IsHealthy', True)
                SIM['curtailRatio'] = pts.get('CURTAIL.Ratio', 0.0)
                SIM['extremeType'] = int(pts.get('EXTREME.SubType', 0))

                # Per-turbine data (C++ registers TURBINE_000 .. TURBINE_009, 0-based)
                for i in range(1, 11):
                    tid = f'WT_{i:02d}'
                    if tid in SIM['turbines']:
                        t = SIM['turbines'][tid]
                        j = i - 1
                        t['powerMW'] = pts.get(f'TURBINE_{j:03d}.Power', t['powerMW'])
                        t['windSpeedMs'] = pts.get(f'TURBINE_{j:03d}.WindSpeed', t['windSpeedMs'])
                        t['rotorSpeedRPM'] = pts.get(f'TURBINE_{j:03d}.RotorSpeed', t['rotorSpeedRPM'])
                        t['pitchAngleDeg'] = pts.get(f'TURBINE_{j:03d}.PitchAngle', t['pitchAngleDeg'])
                        t['targetMW'] = pts.get(f'TURBINE_{j:03d}.Command', t['targetMW'])

                # Write heartbeat keepalive
                now = time.time()
                if now - last_hb_write > 0.5:
                    s.write_point('SCADA.Heartbeat', SIM['heartbeat'])
                    last_hb_write = now

            # Increment cycle
            SIM['cycle'] += 1
            SIM['heartbeat'] += 1

            # ============================================
            # Simulation fallback when SHM is unavailable
            # ============================================
            if not s or not s.connected:
                import math
                ts = SIM['cycle'] * 0.3  # simulation time
                # Wind speed with slow + fast variations
                SIM['windSpeed'] = round(10.0 + 3.0 * math.sin(ts * 0.05) + 1.5 * math.sin(ts * 0.13) + 0.5 * math.sin(ts * 0.3), 1)
                SIM['windSpeed'] = max(0.5, SIM['windSpeed'])
                # Frequency ±0.05Hz
                SIM['frequency'] = round(50.0 + 0.04 * math.sin(ts * 0.08) + 0.02 * math.sin(ts * 0.2), 3)
                SIM['voltage'] = round(1.0 + 0.01 * math.sin(ts * 0.06), 4)
                # Schedule power varies 12–20 MW
                SIM['schedulePower'] = round(15.0 + 5.0 * math.sin(ts * 0.025) + 2.0 * math.sin(ts * 0.07), 1)
                SIM['schedulePower'] = max(3, min(30, SIM['schedulePower']))
                SIM['commHealthy'] = True
                SIM['curtailRatio'] = max(0.0, 0.25 * math.sin(ts * 0.015) + 0.1)
                SIM['extremeType'] = 0
                SIM['emergency'] = False
                SIM['mae'] = round(0.2 + 0.15 * abs(math.sin(ts * 0.1)), 2)
                SIM['rmse'] = round(0.3 + 0.2 * abs(math.sin(ts * 0.1 + 1)), 2)

                # Per-turbine simulation
                total_pwr = 0.0
                total_rpm = 0.0
                total_pitch = 0.0
                for i in range(1, 11):
                    tid = f'WT_{i:02d}'
                    tt = SIM['turbines'][tid]
                    # Each turbine sees slightly different wind (±15%)
                    tw = SIM['windSpeed'] * (0.85 + 0.03 * i) + 0.5 * math.sin(ts * 0.2 + i * 0.7)
                    tw = max(0.5, tw)
                    # Power curve: cut-in 3 m/s, rated 12 m/s, cut-out 25 m/s
                    if tw < 3.0:
                        pwr = 0.0
                    elif tw >= 12.0:
                        pwr = 3.0
                    else:
                        r = (tw - 3.0) / 9.0
                        pwr = 3.0 * (r ** 3)
                    # Add small per-turbine variation
                    pwr += 0.08 * math.sin(ts * 0.3 + i * 1.1)
                    pwr = max(0.0, min(3.0, pwr))
                    rpm = 5.0 + tw * 1.1 + 1.5 * math.sin(ts * 0.15 + i)
                    pitch = max(0.0, tw - 10.0) * 0.4 + 0.5 * math.sin(ts * 0.1 + i * 0.5)
                    pitch = max(0.0, min(8.0, pitch))

                    tt['powerMW'] = round(pwr, 2)
                    tt['windSpeedMs'] = round(tw, 1)
                    tt['rotorSpeedRPM'] = round(rpm, 1)
                    tt['pitchAngleDeg'] = round(pitch, 1)
                    tt['avail'] = tw < 24.0  # Turbine offline if wind > 24 m/s
                    tt['state'] = 'RUN' if tt['avail'] else 'CUTOUT'
                    total_pwr += pwr
                    total_rpm += rpm
                    total_pitch += pitch

                SIM['totalPower'] = round(total_pwr, 1)
                SIM['avgRpm'] = round(total_rpm / 10, 1)
                SIM['avgPitch'] = round(total_pitch / 10, 1)

            else:
                # SHM connected – derive aggregate turbine metrics (farm total
                # already comes from WIND_AGC.TotalPower above; do not overwrite it).
                turbines = SIM['turbines']
                online = [t for t in turbines.values() if t['avail']]
                if online:
                    SIM['avgRpm'] = sum(t['rotorSpeedRPM'] for t in online) / len(online)
                    SIM['avgPitch'] = sum(t['pitchAngleDeg'] for t in online) / len(online)

            # Compute ROCOF
            if HISTORY:
                prev_freq = HISTORY[-1].get('frequency', 50.0)
                SIM['rocof'] = (SIM['frequency'] - prev_freq) / 0.3

            # Update history
            HIST = {
                't': time.time(), 'cycle': SIM['cycle'],
                'frequency': SIM['frequency'], 'voltage': SIM['voltage'],
                'totalPower': SIM['totalPower'], 'schedulePower': SIM['schedulePower'],
                'windSpeed': SIM['windSpeed'], 'scene': SIM['sceneActive'],
                'avgRpm': SIM['avgRpm'], 'avgPitch': SIM['avgPitch'],
                'rocof': SIM['rocof'], 'curtailRatio': SIM['curtailRatio'],
            }
            HISTORY.append(HIST)
            if len(HISTORY) > MAX_HIST:
                HISTORY.pop(0)

            # Scene name
            SCENES = {1: 'S1 稳态', 2: 'S2 风速扰动', 3: 'S3 一次调频', 4: 'S4 爬坡跟踪', 5: 'S5 限电优化', 6: 'S6 安全保护'}
            SIM['sceneName'] = SCENES.get(SIM['sceneActive'], f'S{SIM["sceneActive"]}')

            # Evaluate alerts
            evaluate_alerts()

            # Emit to all clients
            emit_data = {
                'sim': SIM,
                'history': HISTORY[-60:],  # Last 60 cycles
                'alerts': ALERTS[-10:],
            }

            app.config['LATEST_DATA'] = emit_data
            socketio.emit('agc_update', emit_data)

        except Exception as e:
            log.error(f"BG loop error: {e}")

        time.sleep(0.3)


def evaluate_alerts():
    """Evaluate threshold-based alerts."""
    new_alerts = []

    # Frequency deviation
    fd = abs(SIM['frequency'] - 50.0)
    if fd > 0.10:
        new_alerts.append({'level': 'crit', 'msg': f'频率越限: {SIM["frequency"]:.2f} Hz (偏差 {fd:.2f} Hz)', 'ts': time.time()})
    elif fd > 0.05:
        new_alerts.append({'level': 'warn', 'msg': f'频率偏差: {SIM["frequency"]:.2f} Hz', 'ts': time.time()})

    # Voltage
    if SIM['voltage'] < 0.95 or SIM['voltage'] > 1.05:
        new_alerts.append({'level': 'crit', 'msg': f'电压越限: {SIM["voltage"]:.3f} pu', 'ts': time.time()})

    # Wind speed
    if SIM['windSpeed'] >= 25:
        new_alerts.append({'level': 'crit', 'msg': f'极端风况: {SIM["windSpeed"]:.1f} m/s > 切出风速', 'ts': time.time()})
    elif SIM['windSpeed'] < 3:
        new_alerts.append({'level': 'warn', 'msg': f'低风速: {SIM["windSpeed"]:.1f} m/s < 切入风速', 'ts': time.time()})

    # Communication
    if not SIM['commHealthy']:
        new_alerts.append({'level': 'crit', 'msg': 'SCADA 通信中断 — 进入自治模式', 'ts': time.time()})

    # Extreme weather
    if SIM['extremeType'] > 0:
        subtypes = {1: '切出风速', 2: '高湍流', 3: '风暴穿越'}
        new_alerts.append({'level': 'crit', 'msg': f'极端天气: {subtypes.get(SIM["extremeType"], "未知")}', 'ts': time.time()})

    # Curtailment
    if SIM['curtailRatio'] > 0.4:
        new_alerts.append({'level': 'warn', 'msg': f'深度限电: {SIM["curtailRatio"]*100:.0f}%', 'ts': time.time()})

    # Add only new alerts (not duplicates within 2s)
    now = time.time()
    for a in new_alerts:
        exists = any(e['msg'] == a['msg'] and now - e['ts'] < 2.0 for e in ALERTS)
        if not exists:
            ALERTS.insert(0, a)
            CONSOLE_LOG.append({'t': datetime.now().strftime('%H:%M:%S'), 'level': a['level'], 'msg': a['msg']})

    # Trim
    while len(ALERTS) > MAX_ALERTS:
        ALERTS.pop()
    while len(CONSOLE_LOG) > MAX_CONSOLE:
        CONSOLE_LOG.pop(0)


# ============================================================
# Routes
# ============================================================

@app.route('/')
def index():
    """Serve the Vue.js dashboard."""
    return send_from_directory(BASE_DIR, 'index.html')


@app.route('/standalone')
def standalone():
    """Serve the standalone SCADA monitoring page (WindAGC_UI.html)."""
    return send_from_directory(PROJECT_DIR, 'WindAGC_UI.html')


@app.route('/ui/<path:filename>')
def serve_scene(filename):
    """Serve scene HTML pages from PROJECT_DIR/ui/."""
    ui_dir = os.path.join(PROJECT_DIR, 'ui')
    return send_from_directory(ui_dir, filename)


@app.route('/api/status')
def api_status():
    """Full system status."""
    return jsonify({
        'version': '1.0.0',
        'name': 'Wind-AGC Monitoring System',
        'uptime': time.time() - app.config.get('START_TIME', time.time()),
        'sim': SIM,
        'clients': len(socketio.server.manager.rooms.get('/', set()) if hasattr(socketio.server, 'manager') else {}),
    })


@app.route('/api/tags')
def api_tags():
    """List all SHM tags."""
    s = get_shm()
    if s and s.connected:
        return jsonify({'connected': True, 'tagCount': s.tag_count, 'tags': list(s._tag_index.keys())})
    return jsonify({'connected': False, 'tagCount': 0, 'tags': []})


@app.route('/api/history')
def api_history():
    """Return history buffer."""
    limit = request.args.get('limit', 120, type=int)
    return jsonify(HISTORY[-limit:])


@app.route('/api/console/log')
def api_console_log():
    """Return console log."""
    limit = request.args.get('limit', 50, type=int)
    return jsonify(CONSOLE_LOG[-limit:])


@app.route('/api/version')
def api_version():
    """System version info."""
    return jsonify({
        'version': '1.0.0',
        'project': 'Wind-AGC',
        'ratedMW': 30.0,
        'turbineCount': 10,
        'scenes': [
            {'id': 1, 'name': 'S1 稳态', 'desc': '稳态计划跟踪'},
            {'id': 2, 'name': 'S2 风速扰动', 'desc': '风速波动抑制'},
            {'id': 3, 'name': 'S3 一次调频', 'desc': '一次调频 PFR'},
            {'id': 4, 'name': 'S4 爬坡跟踪', 'desc': '调度阶跃爬坡'},
            {'id': 5, 'name': 'S5 限电优化', 'desc': '深度限电优化'},
            {'id': 6, 'name': 'S6 安全保护', 'desc': '通信/极端风况保护'},
        ]
    })


@app.route('/api/set', methods=['POST'])
def api_set():
    """Set a data point value."""
    data = request.get_json(force=True)
    tag = data.get('tag', '')
    value = float(data.get('value', 0))

    s = get_shm()
    if s and s.connected:
        ok = s.write_point(tag, value)
    else:
        ok = False

    # Also update SIM
    if tag == 'GRID.Frequency': SIM['frequency'] = value
    elif tag == 'GRID.Voltage': SIM['voltage'] = value
    elif tag == 'WIND_AGC.WindSpeed': SIM['windSpeed'] = value
    elif tag == 'WIND_AGC.SchedulePower': SIM['schedulePower'] = value
    elif tag == 'COMM.IsHealthy': SIM['commHealthy'] = value > 0.5
    elif tag == 'CURTAIL.Ratio': SIM['curtailRatio'] = value

    return jsonify({'ok': ok, 'tag': tag, 'value': value})


# ============================================================
# SocketIO Events
# ============================================================

@socketio.on('connect')
def on_connect():
    """Client connected."""
    log.info(f"Client connected: {request.sid}")
    emit('welcome', {
        'version': '1.0.0',
        'name': 'Wind-AGC',
        'sim': SIM,
        'history': HISTORY[-60:],
        'alerts': ALERTS[-10:],
    })


@socketio.on('disconnect')
def on_disconnect():
    """Client disconnected."""
    log.info(f"Client disconnected: {request.sid}")


@socketio.on('inject')
def on_inject(data):
    """Handle injection request from web client."""
    inj_type = data.get('type', '')
    log.info(f"Injection: {inj_type} from {request.sid}")

    s = get_shm()
    if not s or not s.connected:
        emit('inject_result', {'ok': False, 'msg': 'SHM not connected'})
        return

    result = {'ok': True, 'type': inj_type}

    if inj_type == 'freq-lo':
        s.set_freq(49.85)
        result['msg'] = '低频注入: 49.85 Hz'
    elif inj_type == 'freq-hi':
        s.set_freq(50.15)
        result['msg'] = '高频注入: 50.15 Hz'
    elif inj_type == 'wind-gust':
        s.set_wind_speed(22.0)
        result['msg'] = '阵风注入: 22 m/s'
    elif inj_type == 'wind-drop':
        s.set_wind_speed(2.0)
        result['msg'] = '风速骤降: 2 m/s'
    elif inj_type == 'volt-hi':
        s.set_voltage(1.08)
        result['msg'] = '电压越限: 1.08 pu'
    elif inj_type == 'extreme-wind':
        s.set_wind_speed(28.0)
        s.set_extreme_weather(1)  # CutOut
        result['msg'] = '极端风况: 28 m/s + 切出保护'
    elif inj_type == 'plan-lo':
        s.set_plan(5.0)
        s.set_curtail_ratio(0.67)
        result['msg'] = '限电: 5 MW (67%限电比)'
    elif inj_type == 'comm-loss':
        s.set_comm(False)
        result['msg'] = '通信中断注入'
    elif inj_type == 'recover':
        s.set_freq(50.0)
        s.set_voltage(1.0)
        s.set_wind_speed(8.0)
        s.set_plan(15.0)
        s.set_comm(True)
        s.set_curtail_ratio(0.0)
        s.set_extreme_weather(0)
        result['msg'] = '所有注入清除 → 等待自愈'
    else:
        result['ok'] = False
        result['msg'] = f'Unknown injection: {inj_type}'

    emit('inject_result', result)


@socketio.on('scene_request')
def on_scene_request(data):
    """Request scene switch."""
    scene_id = int(data.get('scene', 1))
    s = get_shm()
    if s and s.connected:
        s.request_scene(scene_id)
    SIM['sceneActive'] = scene_id
    log.info(f"Scene requested: S{scene_id}")
    emit('scene_result', {'ok': True, 'scene': scene_id})


@socketio.on('s6_select')
def on_s6_select(data):
    """S6 safety strategy selection."""
    strat = int(data.get('strategy', 1))
    s = get_shm()
    if s and s.connected:
        s.set_s6_strategy(strat)
    SIM['s6Strategy'] = strat
    log.info(f"S6 strategy selected: {strat}")
    emit('s6_result', {'ok': True, 'strategy': strat})


@socketio.on('reset')
def on_reset():
    """Full system reset."""
    s = get_shm()
    if s and s.connected:
        s.full_reset()
    SIM['sceneActive'] = 1
    SIM['emergency'] = False
    SIM['commHealthy'] = True
    SIM['frequency'] = 50.0
    SIM['voltage'] = 1.0
    SIM['windSpeed'] = 8.0
    SIM['schedulePower'] = 15.0
    ALERTS.clear()
    log.info("System reset")
    emit('reset_result', {'ok': True, 'msg': '系统已复位'})


# ============================================================
# Singleton startup guard
# ============================================================
_started = False

def start_server(host='127.0.0.1', port=5189, debug=False):
    """Start the Flask+SocketIO server (thread-safe, singleton)."""
    global _started
    if _started:
        log.warning("Server already running")
        return
    _started = True

    app.config['START_TIME'] = time.time()

    # Start background SHM read thread
    bg = threading.Thread(target=bg_read_loop, daemon=True, name='shm-reader')
    bg.start()

    log.info(f"Wind-AGC Server starting on {host}:{port}")
    socketio.run(app, host=host, port=port, debug=debug, allow_unsafe_werkzeug=True, use_reloader=False)


if __name__ == '__main__':
    log.info("Wind-AGC Web Server v1.0")
    start_server(host='127.0.0.1', port=5189, debug=False)
