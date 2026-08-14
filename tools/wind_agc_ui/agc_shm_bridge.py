#!/usr/bin/env python3
"""
Wind-AGC Shared Memory Bridge
Connects to windAGC shared memory (RT_DB) via a Windows named file mapping.

Fixed version — the original (adapted from PV-ESS-AGC) had two bugs:
  1. It opened the mapping with pywin32's ``win32file.OpenFileMapping``, which
     is not available on a stock Python install (caused the runtime
     ``AttributeError``).  This version uses only the stdlib: ``mmap.mmap``
     with ``tagname=...`` (plus ``struct`` for the binary layout).
  2. The memory layout was wrong (copied from PV-ESS-AGC).  The real layout is
     defined in ``src/rt_db/rt_db_structs.h`` and is reproduced below exactly.

Binary layout (MSVC x64, i.e. ``time_t``=8, ``long``=4, ``LONG64``=8):

    SharedMemoryHeader (48 bytes):
        write_count         int64  @  0   (atomic_size_t = volatile LONG64)
        shutdown_requested  int32  @  8   (atomic_bool    = volatile LONG)
        (padding)                  @ 12
        last_updated.tv_sec int64  @ 16
        last_updated.tv_nsec int32 @ 24
        (padding)                  @ 28
        num_data_points     uint64 @ 32   (size_t)
        connected_clients   int32  @ 40   (atomic_int = volatile LONG)
        manager_pid         int32  @ 44   (pid_t)

    DataPoint (120 bytes):
        value        double @   0   (atomic_double = volatile double)
        quality      int64  @   8   (atomic_long  = volatile LONG64)
        sequence     int64  @  16   (atomic_long  = volatile LONG64)
        timestamp.tv_sec  int64  @ 24
        timestamp.tv_nsec int32 @ 32
        (padding)                  @ 36
        point_id     char[64] @ 40
        units        char[16] @104
"""
import mmap
import struct
import os
import time
import threading
from typing import Optional, Dict, List, Any

# ============================================================
# Constants matching src/rt_db/rt_db_structs.h (Windows x64 / MSVC)
# ============================================================
MAX_DATAPOINTS = 20000
MAX_POINT_ID_LEN = 64
MAX_UNIT_LEN = 16

HEADER_SIZE = 48
DATAPOINT_SIZE = 120
# Header + data-point array (the command ring buffer after it is not needed).
SHM_SIZE = HEADER_SIZE + DATAPOINT_SIZE * MAX_DATAPOINTS

# data_quality_t values (src/rt_db/rt_db_structs.h)
QUALITY_BAD = 0
QUALITY_GOOD = 1

# ---- SharedMemoryHeader field offsets ----
HDR_WRITE_COUNT       = 0    # int64
HDR_SHUTDOWN          = 8    # int32
HDR_LAST_UPDATED_SEC  = 16   # int64
HDR_LAST_UPDATED_NSEC = 24   # int32
HDR_NUM_DATA_POINTS   = 32   # uint64
HDR_CONNECTED_CLIENTS = 40   # int32
HDR_MANAGER_PID       = 44   # int32

# ---- DataPoint field offsets ----
DP_VALUE    = 0    # double
DP_QUALITY  = 8    # int64 (LONG64)
DP_SEQUENCE = 16   # int64 (LONG64)
DP_TS_SEC   = 24   # int64
DP_TS_NSEC  = 32   # int32
DP_POINT_ID = 40   # char[64]
DP_UNITS    = 104  # char[16]


def shm_name() -> str:
    """Segment name, overridable via RT_DB_SHM_NAME (mirrors C++ get_shm_name)."""
    name = os.environ.get("RT_DB_SHM_NAME")
    return name if name else "RT_DB_SHARED_MEMORY_WIND"


# Wind-specific tags (indices follow the C++ backend: TURBINE_%03d, 0-based)
INPUT_TAGS = [
    "GRID.Frequency",
    "GRID.FrequencyDelta",
    "SCADA.TotalPower",
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
]

# Per-turbine tags (C++ registers TURBINE_000 .. TURBINE_099, 0-based)
for i in range(0, 10):
    INPUT_TAGS.extend([
        f"TURBINE_{i:03d}.Power",
        f"TURBINE_{i:03d}.WindSpeed",
        f"TURBINE_{i:03d}.RotorSpeed",
        f"TURBINE_{i:03d}.PitchAngle",
        f"TURBINE_{i:03d}.UpMargin",
        f"TURBINE_{i:03d}.DownMargin",
    ])

for i in range(0, 10):
    OUTPUT_TAGS.extend([
        f"TURBINE_{i:03d}.Command",
    ])


class AgcShmReader:
    """Windows named shared memory reader/writer (stdlib mmap + struct + seqlock)."""

    def __init__(self):
        self._mm: Optional[mmap.mmap] = None
        self._tag_index: Dict[str, int] = {}
        self._connected = False
        self._lock = threading.Lock()

    # ------------------------------------------------------------------ attach
    def attach(self) -> bool:
        """Open the RT_DB named file mapping using only the stdlib."""
        name = shm_name()
        try:
            # mmap.mmap(-1, size, tagname=...) does create-or-open on Windows.
            # When rt_db_init.exe already holds the mapping, this opens it.
            m = mmap.mmap(-1, SHM_SIZE, tagname=name, access=mmap.ACCESS_WRITE)
        except (OSError, ValueError) as e:
            print(f"[SHM] attach failed for '{name}': {e}")
            return False

        # Guard against the "create" side of create-or-open: if rt_db_init is
        # not running we would have created a brand-new empty segment. Treat
        # that as not-available and release it so the backend can create it later.
        try:
            num_points = struct.unpack_from('<Q', m, HDR_NUM_DATA_POINTS)[0]
            if num_points == 0:
                m.close()
                print(f"[SHM] mapping '{name}' is uninitialized — is rt_db_init.exe running?")
                return False
        except Exception as e:
            m.close()
            print(f"[SHM] attach verify failed: {e}")
            return False

        self._mm = m
        self._connected = True
        self._build_tag_index()
        print(f"[SHM] Attached to '{name}' — {len(self._tag_index)} tags indexed")
        return True

    # ----------------------------------------------------------- tag indexing
    def _build_tag_index(self):
        """Scan data points, mapping point_id -> absolute SHM offset."""
        self._tag_index.clear()
        for i in range(MAX_DATAPOINTS):
            offset = HEADER_SIZE + i * DATAPOINT_SIZE
            raw = self._mm[offset + DP_POINT_ID:offset + DP_POINT_ID + MAX_POINT_ID_LEN]
            tag = raw.split(b'\x00', 1)[0].decode('utf-8', errors='replace').strip()
            if not tag or tag.startswith("UNUSED_"):
                continue
            self._tag_index[tag] = offset

    # ----------------------------------------------------------------- reads
    def read_point(self, tag: str) -> Optional[float]:
        """Read a single data point value by tag name (seqlock-protected)."""
        if not self._mm:
            return None
        with self._lock:
            offset = self._tag_index.get(tag)
            if offset is None:
                return None
            return self._read_with_seqlock(offset)

    def _read_with_seqlock(self, offset: int, retries: int = 3) -> Optional[float]:
        """Mirror of rt_db_get_value(): sequence is even when data is stable."""
        for _ in range(retries):
            seq_before = struct.unpack_from('<q', self._mm, offset + DP_SEQUENCE)[0]
            if seq_before % 2 != 0:
                continue  # writer in progress
            value = struct.unpack_from('<d', self._mm, offset + DP_VALUE)[0]
            seq_after = struct.unpack_from('<q', self._mm, offset + DP_SEQUENCE)[0]
            if seq_before == seq_after and seq_after % 2 == 0:
                return value
        return None

    def read_all(self) -> Dict[str, Any]:
        """Read the header plus every indexed point (flat ``points`` + grouped views)."""
        result = {"header": {}, "points": {}, "input": {}, "output": {}, "turbines": {}}

        if not self._mm:
            return result

        result["header"] = {
            "write_count":       struct.unpack_from('<Q', self._mm, HDR_WRITE_COUNT)[0],
            "shutdown":          struct.unpack_from('<i', self._mm, HDR_SHUTDOWN)[0],
            "last_updated_sec":  struct.unpack_from('<q', self._mm, HDR_LAST_UPDATED_SEC)[0],
            "last_updated_nsec": struct.unpack_from('<i', self._mm, HDR_LAST_UPDATED_NSEC)[0],
            "num_data_points":   struct.unpack_from('<Q', self._mm, HDR_NUM_DATA_POINTS)[0],
            "connected_clients": struct.unpack_from('<i', self._mm, HDR_CONNECTED_CLIENTS)[0],
            "manager_pid":       struct.unpack_from('<i', self._mm, HDR_MANAGER_PID)[0],
        }

        for tag in self._tag_index:
            val = self.read_point(tag)
            if val is None:
                continue
            result["points"][tag] = val
            if tag.startswith("WIND_AGC.") or tag.startswith("AGC."):
                result["output"][tag] = val
            else:
                # Everything else (incl. TURBINE_*) is consumed as "input".
                result["input"][tag] = val
            if tag.startswith("TURBINE_"):
                result["turbines"][tag] = val

        return result

    # ---------------------------------------------------------------- writes
    def write_point(self, tag: str, value: float) -> bool:
        """Write a value to a data point, mirroring rt_db_set_value()."""
        if not self._mm:
            return False
        try:
            with self._lock:
                offset = self._tag_index.get(tag)
                if offset is None:
                    return False

                # Begin write: bump sequence to odd (writer active).
                seq = struct.unpack_from('<q', self._mm, offset + DP_SEQUENCE)[0]
                struct.pack_into('<q', self._mm, offset + DP_SEQUENCE, seq + 1)

                # Update payload.
                struct.pack_into('<d', self._mm, offset + DP_VALUE, float(value))
                struct.pack_into('<q', self._mm, offset + DP_QUALITY, QUALITY_GOOD)
                now_ns = time.time_ns()
                struct.pack_into('<q', self._mm, offset + DP_TS_SEC, now_ns // 1_000_000_000)
                struct.pack_into('<i', self._mm, offset + DP_TS_NSEC, now_ns % 1_000_000_000)

                # End write: bump sequence to even (data stable).
                struct.pack_into('<q', self._mm, offset + DP_SEQUENCE, seq + 2)

                # Global metadata.
                wc = struct.unpack_from('<Q', self._mm, HDR_WRITE_COUNT)[0]
                struct.pack_into('<Q', self._mm, HDR_WRITE_COUNT, wc + 1)
                return True
        except Exception as e:
            print(f"[SHM] Write error '{tag}': {e}")
        return False

    # ------------------------------------------- high-level write helpers
    def set_plan(self, mw: float):
        self.write_point("WIND_AGC.SchedulePower", mw)

    def set_comm(self, healthy: bool):
        self.write_point("COMM.IsHealthy", 1.0 if healthy else 0.0)

    def set_curtail_ratio(self, ratio: float):
        self.write_point("CURTAIL.Ratio", max(0.0, min(1.0, ratio)))

    def set_extreme_weather(self, sub_type: int):
        self.write_point("EXTREME.SubType", float(sub_type))

    def set_freq(self, hz: float):
        self.write_point("GRID.Frequency", hz)

    def set_voltage(self, pu: float):
        # No GRID.Voltage point exists in the C++ registration; best-effort no-op.
        pass

    def set_wind_speed(self, ms: float):
        self.write_point("WIND_AGC.WindSpeed", ms)

    def request_scene(self, scene_id: int):
        self.write_point("WIND_AGC.Mode", float(scene_id))

    def set_s6_strategy(self, strategy: int):
        # No AGC.S6Strategy point exists; best-effort no-op.
        pass

    def set_turbine_setpoint(self, turbine_id: int, mw: float):
        self.write_point(f"TURBINE_{turbine_id:03d}.Command", mw)

    def full_reset(self):
        self.write_point("WIND_AGC.TotalPower", 0.0)
        self.write_point("WIND_AGC.Setpoint", 0.0)
        self.write_point("WIND_AGC.Mode", 1.0)
        for i in range(0, 10):
            self.write_point(f"TURBINE_{i:03d}.Command", 0.0)

    # --------------------------------------------------------------- teardown
    def detach(self):
        self._connected = False
        if self._mm:
            try:
                self._mm.close()
            except Exception:
                pass
            self._mm = None
        self._tag_index.clear()

    @property
    def connected(self) -> bool:
        return self._connected

    @property
    def tag_count(self) -> int:
        return len(self._tag_index)


# ============================================================
# Smoke-test entry
# ============================================================
if __name__ == "__main__":
    reader = AgcShmReader()
    if reader.attach():
        print(f"Connected: {reader.tag_count} tags")
        print("header:", reader.read_all()["header"])
        for t in INPUT_TAGS[:10]:
            print(f"  {t} = {reader.read_point(t)}")
        reader.detach()
    else:
        print("Not connected — run rt_db_init.exe first")
