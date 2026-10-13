#!/usr/bin/env python3
"""Smoke-test the NRM-BFM serial protocol over USB CDC-ACM.

The device appears as a plain serial port (macOS: /dev/cu.usbmodem*, Linux:
/dev/ttyACM0), distinct from the USB-Serial/JTAG port used by `idf.py monitor`.
Frames are a 4-byte little-endian length prefix followed by a JSON body.

Usage:
    pip install pyserial
    python3 smoke.py /dev/cu.usbmodemXXXX

Exercises: CAPABILITIES, STATE, CONFIG (read + write), CONTROL, ACTION (stop/guide/sync),
and the idempotency of a repeated ACTION id.
"""

import json
import struct
import sys

import serial

from client import detect_serial_port


class Mount:
    def __init__(self, port):
        self.ser = serial.Serial(port, timeout=2.0)

    def _send(self, obj):
        body = json.dumps(obj).encode("utf-8")
        self.ser.write(struct.pack("<I", len(body)) + body)

    def _read_frame(self):
        header = self.ser.read(4)
        if len(header) < 4:
            raise TimeoutError("no response (timeout)")
        length = struct.unpack("<I", header)[0]
        body = self.ser.read(length)
        if len(body) < length:
            raise IOError("short frame")
        return json.loads(body.decode("utf-8"))

    def request(self, obj):
        """Send a request and return the response frame."""
        self._send(obj)
        return self._read_frame()


def check(label, cond):
    print(("PASS" if cond else "FAIL"), "-", label)
    if not cond:
        raise SystemExit(1)


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else detect_serial_port()
    if not port:
        print("No NRM-BFM serial port found. Connect and flash, or pass the port "
              "as an argument.")
        sys.exit(1)
    m = Mount(port)

    caps = m.request({"type": "capabilities", "id": 1})
    check("capabilities ok", caps["ok"] is True)
    check("capabilities axes", caps["axes"] == ["ra", "dec"])
    check("capabilities sync unsupported", caps["caps"]["sync"] is False)
    boot_id = caps["boot_id"]

    state = m.request({"type": "state", "id": 2})
    check("state ok", state["ok"] is True)
    check("state has boot_id", state["boot_id"] == boot_id)
    check("state has steps", isinstance(state["ra_steps"], int))
    check("state has power", "power" in state)

    cfg = m.request({"type": "config", "id": 3})
    check("config read ok", cfg["ok"] is True)
    check("config has site", "lat" in cfg["config"])

    cfg = m.request({"type": "config", "id": 4, "lat": -32.89, "lon": -68.83,
                     "elevation": 750, "utc": "2026-10-08T12:00:00Z"})
    check("config write ok", cfg["ok"] is True)
    check("config persisted", abs(cfg["config"]["lat"] + 32.89) < 0.01)

    state = m.request({"type": "state", "id": 5})
    check("time valid after utc", state["time_valid"] is True)

    ctrl = m.request({"type": "control", "id": 6, "tracking": "sidereal"})
    check("control tracking ok", ctrl["ok"] is True)
    state = m.request({"type": "state", "id": 7})
    check("tracking active", state["tracking"] == "sidereal")

    ctrl = m.request({"type": "control", "id": 8, "manual_ra_dps": 0.5})
    check("control manual ok", ctrl["ok"] is True)

    stop = m.request({"type": "action", "id": 9, "action": "stop", "scope": "all"})
    check("stop ok", stop["ok"] is True)

    guide = m.request({"type": "action", "id": 10, "action": "guide",
                       "direction": "east", "duration_ms": 200})
    check("guide accepted", guide["ok"] is True)

    sync = m.request({"type": "action", "id": 11, "action": "sync",
                      "ra": 1.0, "dec": -33.0})
    check("sync rejected", sync["ok"] is False)

    # Idempotency: a repeated id must be acknowledged without re-executing.
    again = m.request({"type": "action", "id": 9, "action": "stop", "scope": "all"})
    check("duplicate id acknowledged", again["ok"] is True)

    print("\nAll smoke checks passed.")


if __name__ == "__main__":
    main()
