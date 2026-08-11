#!/usr/bin/env python3
"""
Wind-AGC Shared Memory Bridge
Adapated from PV-ESS-AGC's agc_shm_bridge.py
Connects to windAGC shared memory (RT_DB) via Windows named file mapping
Uses seqlock protocol for atomic reads, struct for binary layout
"""
import ctypes
import mmap
import struct
import os
import time
import threading
from dataclasses import dataclass, field
from typing import Optional, Dict, List, Any

# ============================================================
# Constants matching C++ CommonTypes.h and RT_DB layout
# ============================================================
SHM_NAME = "RT_DB_SHARED_MEMORY"
HEADER_SIZE = 48
DATAPOINT_SIZE = 120
MAX_DATAPOINTS = 20000
SHM_SIZE = HEADER_SIZE + DATAPOINT_SIZE * MAX_DATAPOINTS  # ~2.4MB

# Wind-specific tags
INPUT_TAGS = [
    "GRID.Frequency",
    "GRID.Voltage",
    "PCC.TotalPower",
    "SCADA.PlanPower",
    "SCADA.Heartbeat",
    "WIND_AGC.WindSpeed",
    "WIND_AGC.SchedulePower",
    "COMM.IsHealthy",
    "EXTREME.SubType",
    "CURTAIL.Ratio",
]

OUTPUT_TAGS = [
    "WIND_AGC.TotalPower",
    "WIND_AGC.Setpoint",
    "WIND_AGC.Mode",
    "AGC.SceneActive",
    "AGC.S6Strategy",
    "AGC.WindSpeedBaseline",
]

# Per-turbine tags (10 turbines)
for i in range(1, 11):
    INPUT_TAGS.extend([
        f"TURBINE_{i:02d}.Power",
        f"TURBINE_{i:02d}.WindSpeed",
        f"TURBINE_{i:02d}.RotorSpeed",
        f"TURBINE_{i:02d}.PitchAngle",
        f"TURBINE_{i:02d}.Torque",
        f"TURBINE_{i:02d}.State",
        f"TURBINE_{i:02d}.Role",
        f"TURBINE_{i:02d}.SafetyIndex",
    ])

for i in range(1, 11):
    OUTPUT_TAGS.extend([
        f"TURBINE_{i:02d}.Setpoint",
        f"TURBINE_{i:02d}.Command",
    ])


@dataclass
class ShmHeader:
    """Shared memory header layout (48 bytes)"""
    magic: int = 0
    version: int = 0
    seq_lock: int = 0
    point_count: int = 0
    timestamp: int = 0
    status: int = 0
    reserved: List[int] = field(default_factory=lambda: [0] * 6)


class AgcShmReader:
    """Windows named shared memory reader using mmap + struct with seqlock protocol."""

    def __init__(self):
        self._fd = None
        self._mm: Optional[mmap.mmap] = None
        self._tag_index: Dict[str, int] = {}
        self._connected = False
        self._lock = threading.Lock()

    def attach(self) -> bool:
        """Attach to RT_DB shared memory."""
        try:
            import win32file
            import win32con
            import pywintypes

            # Open existing file mapping
            handle = win32file.OpenFileMapping(
                win32con.FILE_MAP_READ | win32con.FILE_MAP_WRITE,
                False,
                SHM_NAME
            )
            if not handle:
                print(f"[SHM] File mapping '{SHM_NAME}' not found — is rt_db_init.exe running?")
                return False

            self._mm = mmap.mmap(handle, SHM_SIZE, access=mmap.ACCESS_WRITE)
            self._fd = handle
            self._connected = True

            # Build tag index by scanning data points
            self._build_tag_index()
            print(f"[SHM] Attached to '{SHM_NAME}' — {len(self._tag_index)} tags indexed")
            return True

        except ImportError:
            # Fallback: try reading via ctypes
            print("[SHM] pywin32 not found, trying ctypes fallback...")
            return self._attach_ctypes()
        except Exception as e:
            print(f"[SHM] Attach failed: {e}")
            return False

    def _attach_ctypes(self) -> bool:
        """Fallback: attach via ctypes kernel32."""
        try:
            kernel32 = ctypes.windll.kernel32
            PAGE_READWRITE = 0x04
            FILE_MAP_READ = 0x0004
            FILE_MAP_WRITE = 0x0002

            handle = kernel32.OpenFileMappingW(
                FILE_MAP_READ | FILE_MAP_WRITE,
                False,
                SHM_NAME
            )
            if not handle:
                print(f"[SHM] ctypes: file mapping '{SHM_NAME}' not found")
                return False

            self._fd = handle
            # Read via ctypes directly (no mmap)
            self._connected = True
            self._build_tag_index_ctypes()
            print(f"[SHM] ctypes: attached to '{SHM_NAME}' — {len(self._tag_index)} tags indexed")
            return True
        except Exception as e:
            print(f"[SHM] ctypes fallback failed: {e}")
            return False

    def _build_tag_index(self):
        """Build tag→offset index by reading SHM data point headers."""
        # Read the data point table sequentially
        for i in range(MAX_DATAPOINTS):
            offset = HEADER_SIZE + i * DATAPOINT_SIZE
            try:
                # Tag name is stored as 64-byte string at offset+0
                tag_bytes = self._mm[offset:offset + 64]
                tag_name = tag_bytes.rstrip(b'\x00').decode('utf-8', errors='replace').strip()
                if tag_name:
                    self._tag_index[tag_name] = offset
            except Exception:
                break

    def _build_tag_index_ctypes(self):
        """Build tag index via ctypes (read 48 bytes at a time for efficiency)."""
        pass  # Deferred — use read_point for individual access

    def read_point(self, tag: str) -> Optional[float]:
        """Read a single data point value by tag name. Returns None if not found or locked."""
        with self._lock:
            if self._mm and tag in self._tag_index:
                return self._read_with_seqlock(self._tag_index[tag])
            elif self._fd and self._mm is None:
                # ctypes fallback
                return self._read_ctypes(tag)
            return None

    def _read_with_seqlock(self, offset: int, retries: int = 3) -> Optional[float]:
        """Seqlock-protected read of a float value from SHM."""
        for _ in range(retries):
            # Read sequence counter from header
            header = struct.unpack_from('<IIIIII', self._mm, 0)
            seq_before = header[2]

            # Read value (float stored at offset+64)
            value_offset = offset + 64
            value = struct.unpack_from('<d', self._mm, value_offset)[0]

            # Re-read sequence counter
            header = struct.unpack_from('<IIIIII', self._mm, 0)
            seq_after = header[2]

            if seq_before == seq_after and seq_before % 2 == 0:
                return value

        return None

    def read_all(self) -> Dict[str, Any]:
        """Read all indexed data points. Returns dict with values."""
        result = {"header": {}, "input": {}, "output": {}, "turbines": {}}

        # Read header
        if self._mm:
            header = struct.unpack_from('<IIIIII', self._mm, 0)
            result["header"] = {
                "magic": header[0], "version": header[1],
                "seq_lock": header[2], "point_count": header[3],
                "timestamp": header[4], "status": header[5]
            }

        # Read all tags
        for tag in self._tag_index:
            val = self.read_point(tag)
            if val is not None:
                if tag.startswith("TURBINE_"):
                    result["turbines"][tag] = val
                elif tag.startswith("WIND_AGC.") or tag.startswith("AGC."):
                    result["output"][tag] = val
                else:
                    result["input"][tag] = val

        return result

    def _read_ctypes(self, tag: str) -> Optional[float]:
        """ctypes fallback single-point read."""
        return None  # Simplified — full impl requires offset tracking

    def write_point(self, tag: str, value: float) -> bool:
        """Write a float value to a data point via SHM."""
        if not self._connected:
            return False
        try:
            with self._lock:
                if self._mm and tag in self._tag_index:
                    value_offset = self._tag_index[tag] + 64
                    self._mm[value_offset:value_offset + 8] = struct.pack('<d', value)
                    return True
        except Exception as e:
            print(f"[SHM] Write error '{tag}': {e}")
        return False

    # ---- High-level write helpers (wind-specific) ----

    def set_plan(self, mw: float):
        """Set dispatch plan (MW)."""
        self.write_point("SCADA.PlanPower", mw)
        self.write_point("WIND_AGC.SchedulePower", mw)

    def set_comm(self, healthy: bool):
        """Set communication health flag."""
        self.write_point("COMM.IsHealthy", 1.0 if healthy else 0.0)

    def set_curtail_ratio(self, ratio: float):
        """Set curtailment ratio (0.0–1.0)."""
        self.write_point("CURTAIL.Ratio", max(0.0, min(1.0, ratio)))

    def set_extreme_weather(self, sub_type: int):
        """Set extreme weather sub-type (0=None, 1=CutOut, 2=HighTurb, 3=StormRide)."""
        self.write_point("EXTREME.SubType", float(sub_type))

    def set_freq(self, hz: float):
        """Set grid frequency (Hz)."""
        self.write_point("GRID.Frequency", hz)

    def set_voltage(self, pu: float):
        """Set grid voltage (per unit)."""
        self.write_point("GRID.Voltage", pu)

    def set_wind_speed(self, ms: float):
        """Set average wind speed (m/s)."""
        self.write_point("WIND_AGC.WindSpeed", ms)

    def request_scene(self, scene_id: int):
        """Request scene switch (1–6)."""
        self.write_point("AGC.SceneActive", float(scene_id))

    def set_s6_strategy(self, strategy: int):
        """Set S6 safety strategy (1=Hold, 2=RampDown, 3=Feather)."""
        self.write_point("AGC.S6Strategy", float(strategy))

    def set_turbine_setpoint(self, turbine_id: int, mw: float):
        """Set power setpoint for a specific turbine."""
        tag = f"TURBINE_{turbine_id:02d}.Setpoint"
        self.write_point(tag, mw)

    def full_reset(self):
        """Reset all output signals to defaults."""
        self.write_point("WIND_AGC.TotalPower", 0.0)
        self.write_point("WIND_AGC.Setpoint", 0.0)
        self.write_point("WIND_AGC.Mode", 1.0)
        self.write_point("AGC.SceneActive", 1.0)
        self.write_point("AGC.S6Strategy", 0.0)
        self.write_point("AGC.WindSpeedBaseline", 8.0)
        for i in range(1, 11):
            self.write_point(f"TURBINE_{i:02d}.Setpoint", 0.0)
            self.write_point(f"TURBINE_{i:02d}.Command", 0.0)

    def detach(self):
        """Detach from shared memory."""
        self._connected = False
        if self._mm:
            self._mm.close()
            self._mm = None
        self._tag_index.clear()

    @property
    def connected(self) -> bool:
        return self._connected

    @property
    def tag_count(self) -> int:
        return len(self._tag_index)


# ============================================================
# Test / smoke-test entry
# ============================================================
if __name__ == "__main__":
    reader = AgcShmReader()
    if reader.attach():
        print(f"Connected: {reader.tag_count} tags")
        for t in INPUT_TAGS[:10]:
            v = reader.read_point(t)
            print(f"  {t} = {v}")
        reader.detach()
    else:
        print("Not connected — run rt_db_init.exe first")
