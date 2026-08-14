#!/usr/bin/env python3
"""
Wind-AGC Qt Desktop Shell
Adapted from PV-ESS-AGC's agc_qt_app.py
PySide6 QWebEngineView → Flask server on port 5189
Auto-launches C++ backend if not running
"""
import os
import sys
import time
import signal
import subprocess
import threading
import argparse
from pathlib import Path

# ============================================================
# Configuration
# ============================================================
BASE_DIR = Path(__file__).resolve().parent        # tools/wind_agc_ui/
PROJECT_DIR = BASE_DIR.parent.parent             # windAGC-main/
BUILD_DIR = PROJECT_DIR / "build" / "bin" / "Release"

SERVER_HOST = "127.0.0.1"
SERVER_PORT = 5189
SERVER_URL = f"http://{SERVER_HOST}:{SERVER_PORT}"

# Process names to check/kill
BACKEND_PROCESSES = ["rt_db_init.exe", "unified_agc.exe"]


# ============================================================
# Process management
# ============================================================
def clean_stale_processes():
    """Kill stale backend processes from previous runs (no popups)."""
    killed = 0
    for name in BACKEND_PROCESSES:
        try:
            result = subprocess.run(
                ['taskkill', '/F', '/IM', name, '/T'],
                capture_output=True, text=True, timeout=5,
                creationflags=subprocess.CREATE_NO_WINDOW
            )
            if result.returncode == 0:
                killed += 1
                print(f"[CLEAN] Killed stale {name}")
        except Exception:
            pass
    if killed:
        print(f"[CLEAN] Killed {killed} stale process(es)")
    else:
        print(f"[CLEAN] No stale processes found")


def kill_port(port):
    """Kill any process listening on the given port (Windows)."""
    try:
        result = subprocess.run(
            ['netstat', '-ano'],
            capture_output=True, text=True, timeout=5,
            creationflags=subprocess.CREATE_NO_WINDOW
        )
        for line in result.stdout.split('\n'):
            if f':{port}' in line and 'LISTENING' in line:
                parts = line.strip().split()
                pid = parts[-1]
                subprocess.run(
                    ['taskkill', '/F', '/PID', pid],
                    capture_output=True, text=True, timeout=5,
                    creationflags=subprocess.CREATE_NO_WINDOW
                )
                print(f"[CLEAN] Killed PID {pid} on port {port}")
                time.sleep(1)  # Wait for socket release
    except Exception:
        pass


def find_executable(name: str) -> Path:
    """Find an executable in build directory or PATH."""
    path = BUILD_DIR / name
    if path.exists():
        return path
    # Try current directory
    path = BASE_DIR / name
    if path.exists():
        return path
    return Path(name)  # Fallback to PATH


def ensure_rt_processes():
    """Ensure RT_DB init and unified_agc are running (background, no popups)."""
    procs = []

    def _proc_running(name):
        try:
            result = subprocess.run(
                ['tasklist', '/FI', f'IMAGENAME eq {name}'],
                capture_output=True, text=True, timeout=3,
                creationflags=subprocess.CREATE_NO_WINDOW)
            return name in result.stdout
        except Exception:
            return False

    # Check rt_db_init.exe
    if _proc_running('rt_db_init.exe'):
        print("[PROC] rt_db_init.exe already running")
    else:
        exe = find_executable("rt_db_init.exe")
        if exe.exists():
            try:
                p = subprocess.Popen([str(exe)], creationflags=subprocess.CREATE_NO_WINDOW)
                procs.append(('rt_db_init', p))
                print(f"[PROC] Started rt_db_init.exe (PID {p.pid})")
                time.sleep(1)
            except Exception as e:
                print(f"[PROC] rt_db_init launch failed: {e}")

    # Check unified_agc.exe
    if _proc_running('unified_agc.exe'):
        print("[PROC] unified_agc.exe already running")
    else:
        exe = find_executable("unified_agc.exe")
        if exe.exists():
            try:
                p = subprocess.Popen([str(exe)], creationflags=subprocess.CREATE_NO_WINDOW)
                procs.append(('unified_agc', p))
                print(f"[PROC] Started unified_agc.exe (PID {p.pid})")
                time.sleep(1)
            except Exception as e:
                print(f"[PROC] unified_agc launch failed: {e}")

    return procs


def _shm_exists() -> bool:
    """Check if shared memory is available (stdlib only — no pywin32)."""
    try:
        import ctypes
        name = os.environ.get("RT_DB_SHM_NAME", "RT_DB_SHARED_MEMORY_WIND")
        kernel32 = ctypes.windll.kernel32
        kernel32.OpenFileMappingW.restype = ctypes.c_void_p
        kernel32.OpenFileMappingW.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_wchar_p]
        FILE_MAP_READ = 0x0004
        handle = kernel32.OpenFileMappingW(FILE_MAP_READ, False, name)
        if handle:
            kernel32.CloseHandle(ctypes.c_void_p(handle))
            return True
    except Exception:
        pass
    return False


def _server_alive(url: str, timeout: float = 1.0) -> bool:
    """Check if Flask server is responding."""
    try:
        import urllib.request
        req = urllib.request.Request(f"{url}/api/version")
        urllib.request.urlopen(req, timeout=timeout)
        return True
    except Exception:
        return False


# ============================================================
# Qt Application
# ============================================================
def build_window(args, backend_ok=True):
    """Build the Qt main window with QWebEngineView."""
    print("[QT] Starting build_window...")
    try:
        from PySide6.QtWidgets import QApplication, QMainWindow, QVBoxLayout, QWidget, QLabel, QProgressBar
        from PySide6.QtWebEngineWidgets import QWebEngineView
        from PySide6.QtWebEngineCore import QWebEngineSettings
        from PySide6.QtCore import QUrl, Qt, QTimer
        from PySide6.QtGui import QIcon
        print("[QT] Imports OK")
    except ImportError as e:
        print(f"ERROR: PySide6 not installed. Run: pip install PySide6 PySide6-WebEngine\n  Detail: {e}")
        sys.exit(1)

    # Disable GPU if requested or by default on Windows for stability
    if args.no_gpu or sys.platform == 'win32':
        os.environ.setdefault('QTWEBENGINE_CHROMIUM_FLAGS', '--disable-gpu')
        print("[QT] GPU disabled for stability")

    app = QApplication(sys.argv)
    app.setApplicationName("Wind-AGC Monitor")
    app.setOrganizationName("WindAGC")
    print("[QT] QApplication created")

    # Main window
    window = QMainWindow()
    window.setWindowTitle("Wind-AGC 风电场自动发电控制系统 — 工业监控终端 v1.0")
    window.resize(1440, 900)
    window.setMinimumSize(1024, 640)
    print(f"[QT] Window created, flags: {int(window.windowFlags())}")

    # Central widget with loading screen
    central = QWidget()
    window.setCentralWidget(central)
    layout = QVBoxLayout(central)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    # Status sub-label
    status_label = QLabel("")
    status_label.setAlignment(Qt.AlignCenter)
    status_label.setStyleSheet("color: #556677; font-size: 12px; background: #0f161f; padding: 0 20px 12px 20px;")
    layout.addWidget(status_label)

    # Loading indicator
    loading_label = QLabel("Wind-AGC 风电场监控系统")
    loading_label.setAlignment(Qt.AlignCenter)
    loading_label.setStyleSheet("color: #00d4c8; font-size: 20px; font-weight: bold; background: #0f161f; padding: 40px 20px 4px 20px;")
    layout.addWidget(loading_label)

    sub_label = QLabel("正在启动服务...")
    sub_label.setAlignment(Qt.AlignCenter)
    sub_label.setStyleSheet("color: #8899b0; font-size: 14px; background: #0f161f; padding: 0 20px 20px 20px;")
    layout.addWidget(sub_label)

    progress = QProgressBar()
    progress.setRange(0, 100)
    progress.setValue(0)
    progress.setStyleSheet("""
        QProgressBar { background: #0c141e; border: 1px solid #1e3048; border-radius: 3px; height: 6px; text-align: center; margin: 0 40px; }
        QProgressBar::chunk { background: #00d4c8; border-radius: 3px; }
    """)
    layout.addWidget(progress)

    # Web engine view
    try:
        web_view = QWebEngineView()
        web_view.setVisible(False)
        layout.addWidget(web_view)
        print("[QT] QWebEngineView created OK")
    except Exception as e:
        print(f"[QT] ERROR creating QWebEngineView: {e}")
        import traceback
        traceback.print_exc()
        # Fallback: show error in label
        loading_label.setText("Web引擎初始化失败")
        sub_label.setText(f"错误: {e}\n请尝试 --no-gpu 参数")
        raise

    # Configure web engine
    settings = web_view.settings()
    settings.setAttribute(QWebEngineSettings.LocalStorageEnabled, True)
    settings.setAttribute(QWebEngineSettings.JavascriptEnabled, True)
    settings.setAttribute(QWebEngineSettings.ErrorPageEnabled, False)
    # Disable GPU by default on Windows for stability
    settings.setAttribute(QWebEngineSettings.Accelerated2dCanvasEnabled, False)
    settings.setAttribute(QWebEngineSettings.WebGLEnabled, False)

    if args.no_gpu:
        settings.setAttribute(QWebEngineSettings.Accelerated2dCanvasEnabled, False)
        settings.setAttribute(QWebEngineSettings.WebGLEnabled, False)

    _timer_stopped = False
    _ui_loaded = False

    def stop_timer():
        nonlocal _timer_stopped
        if not _timer_stopped:
            timer.stop()
            _timer_stopped = True

    # Wait for server, then load
    def check_server():
        nonlocal _ui_loaded
        retry_count = getattr(check_server, 'count', 0) + 1
        check_server.count = retry_count
        progress.setValue(min(retry_count * 5, 90))

        # Update status text based on stage
        if retry_count <= 3:
            status_label.setText(f"启动 Web 服务... ({retry_count}/30)")
        elif retry_count <= 10:
            status_label.setText(f"等待服务就绪... ({retry_count}/30)")
        elif retry_count <= 20:
            status_label.setText(f"服务响应延迟，继续等待... ({retry_count}/30)")
        else:
            status_label.setText(f"即将超时，请检查端口 5189... ({retry_count}/30)")

        if _server_alive(SERVER_URL):
            stop_timer()
            progress.setValue(100)
            sub_label.setText("服务就绪 — 加载监控界面...")
            sub_label.setStyleSheet("color: #00e676; font-size: 14px; background: #0f161f; padding: 0 20px 20px 20px;")
            status_label.setText("服务连接成功")
            status_label.setStyleSheet("color: #00e676; font-size: 12px; background: #0f161f; padding: 0 20px 12px 20px;")
            QTimer.singleShot(300, load_ui)
            _ui_loaded = True
        elif retry_count > 30:
            stop_timer()
            sub_label.setText("服务启动超时")
            sub_label.setStyleSheet("color: #ff3d3d; font-size: 14px; background: #0f161f; padding: 0 20px 20px 20px;")
            status_label.setText("请确认端口 5189 未被占用，然后重新启动")
            status_label.setStyleSheet("color: #ffb74d; font-size: 12px; background: #0f161f; padding: 0 20px 12px 20px;")
        elif retry_count == 15:
            # At 15s, try to start server again as fallback
            status_label.setText(f"尝试重新启动服务... ({retry_count}/30)")
            try:
                from agc_ui_server import start_server, _started
                import threading as _thr
                if not _server_alive(SERVER_URL):
                    _thr.Thread(target=start_server, kwargs={'host': SERVER_HOST, 'port': SERVER_PORT, 'debug': False}, daemon=True).start()
            except:
                pass

    check_server.count = 0  # Init retry counter on the function itself

    def load_ui():
        stop_timer()  # CRITICAL: stop timer so we don't reload every second
        print("[QT] load_ui called, loading:", SERVER_URL)
        url = QUrl(SERVER_URL)
        web_view.load(url)
        web_view.setVisible(True)
        loading_label.setVisible(False)
        sub_label.setVisible(False)
        progress.setVisible(False)
        status_label.setVisible(False)
        print("[QT] UI loaded")

        def on_load_finished(ok):
            if not ok:
                sub_label.setText("页面加载失败 — 点击下方重试")
                sub_label.setStyleSheet("color: #ffb74d; font-size: 14px; background: #0f161f; padding: 0 20px 20px 20px; cursor: pointer;")
                sub_label.setVisible(True)
                sub_label.mousePressEvent = lambda e: web_view.reload()
            else:
                print("[UI] Dashboard loaded successfully")

        web_view.loadFinished.connect(on_load_finished)

    timer = QTimer()
    timer.timeout.connect(check_server)
    timer.start(1000)

    # Show window
    print(f"[QT] Calling window.show(), flags={int(window.windowFlags())}, isVisible={window.isVisible()}")
    window.show()
    print(f"[QT] window.show() done, isVisible={window.isVisible()}, size={window.size().width()}x{window.size().height()}")

    # Smoke test
    if args.smoke_test:
        QTimer.singleShot(5000, app.quit)

    print("[QT] build_window complete, returning app+window")
    return app, window


# ============================================================
# Main
# ============================================================
def main():
    parser = argparse.ArgumentParser(description='Wind-AGC Qt Desktop Shell')
    parser.add_argument('--clean', action='store_true', help='Kill stale processes before start')
    parser.add_argument('--smoke-test', action='store_true', help='Auto-exit after 5s (for CI)')
    parser.add_argument('--no-gpu', action='store_true', help='Disable GPU acceleration')
    parser.add_argument('--no-backend', action='store_true', help='Skip C++ backend launch')
    parser.add_argument('--server-only', action='store_true', help='Launch server only, no GUI')
    args = parser.parse_args()

    # 区分光电/风电共享内存段名: C++ 代码通过 RT_DB_SHM_NAME 环境变量读取
    os.environ.setdefault("RT_DB_SHM_NAME", "RT_DB_SHARED_MEMORY_WIND")

    print("=" * 60)
    print("  Wind-AGC Wind Farm AGC Monitoring Terminal v1.0")
    print("  Qt Desktop Shell + Flask Web Server on port %s" % SERVER_PORT)
    print("=" * 60)

    # Always kill stale port to ensure clean start
    kill_port(SERVER_PORT)

    # Clean stale processes
    if args.clean:
        clean_stale_processes()

    # Launch C++ backend
    backend_procs = []
    if not args.no_backend:
        backend_procs = ensure_rt_processes()
    backend_ok = len(backend_procs) > 0

    # Start Flask server in daemon thread
    print(f"[SVR] Starting Flask server on {SERVER_URL} ...")
    from agc_ui_server import start_server, _started as _srv_started
    # Reset the singleton guard so we can restart
    import agc_ui_server
    agc_ui_server._started = False
    server_thread = threading.Thread(
        target=start_server,
        kwargs={'host': SERVER_HOST, 'port': SERVER_PORT, 'debug': False},
        daemon=True,
        name='flask-server'
    )
    server_thread.start()
    time.sleep(2)

    if args.server_only:
        print(f"[SVR] Server-only mode - running on {SERVER_URL}")
        print("[SVR] Press Ctrl+C to stop")
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            print("[SVR] Shutting down...")
        return

    # Launch Qt GUI
    print("[MAIN] Launching Qt GUI...")
    try:
        app, window = build_window(args, backend_ok)
        print("[MAIN] build_window returned successfully")
    except Exception as e:
        print(f"[MAIN] FATAL: build_window crashed: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)

    # Cleanup on exit
    def cleanup():
        print("[EXIT] Shutting down...")
        for name, proc in backend_procs:
            try:
                proc.terminate()
                proc.wait(timeout=3)
            except Exception:
                pass

    app.aboutToQuit.connect(cleanup)

    print("[MAIN] Starting Qt event loop (app.exec)...")
    try:
        sys.exit(app.exec())
    except KeyboardInterrupt:
        cleanup()
        sys.exit(0)
    except Exception as e:
        print(f"[MAIN] Qt event loop crashed: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)


if __name__ == '__main__':
    main()
