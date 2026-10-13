"""Serial protocol client.

Three kinds of failure are separated:
  - TransportError: could not talk to the device (serial/timeout).
  - DeviceError: the device answered ok=false.
  - AssertionError: the case assertion failed.

Frames are a 4-byte little-endian length prefix followed by a JSON body.
"""
import json
import struct
import time

import serial
import serial.tools.list_ports


class TransportError(RuntimeError):
    pass


class DeviceError(RuntimeError):
    def __init__(self, message, raw=""):
        super().__init__(f"proto rejected: {message}")
        self.message = message
        self.raw = raw


# USB identity of the NRM-BFM CDC-ACM device (see sdkconfig.defaults).
NRM_VID = 0x303A
NRM_PID = 0x4001


def detect_serial_port():
    """Return the NRM-BFM CDC-ACM port, or None when it is not found.

    Prefers an exact USB VID/PID match, then falls back to the first
    usbmodem (macOS) / ttyACM (Linux) device.
    """
    ports = list(serial.tools.list_ports.comports())
    for p in ports:
        if getattr(p, "vid", None) == NRM_VID and getattr(p, "pid", None) == NRM_PID:
            return p.device
    for p in ports:
        dev = p.device or ""
        if "usbmodem" in dev or "ttyACM" in dev:
            return p.device
    return None


class ProtoClient:
    """Cliente del protocolo de montura sobre el puerto serial CDC-ACM."""

    def __init__(self, port, timeout=2.0, baudrate=115200):
        self.port = port
        try:
            self.ser = serial.Serial(port, timeout=timeout)
        except Exception as e:
            raise TransportError(f"open {port}: {type(e).__name__}: {e}")
        self._id = 0

    # ── framing ──────────────────────────────────────────────────
    def _send(self, obj):
        body = json.dumps(obj).encode("utf-8")
        self.ser.write(struct.pack("<I", len(body)) + body)
        self.ser.flush()

    def _read_frame(self):
        header = self.ser.read(4)
        if len(header) < 4:
            raise TransportError("no response (serial timeout)")
        length = struct.unpack("<I", header)[0]
        if length == 0 or length > 4096:
            raise TransportError(f"implausible frame length {length}")
        body = self.ser.read(length)
        if len(body) < length:
            raise TransportError("short frame")
        try:
            return json.loads(body.decode("utf-8"))
        except Exception as e:
            raise TransportError(f"non-JSON frame: {body[:80]!r}")

    def _next_id(self):
        self._id += 1
        return self._id

    def request(self, obj):
        """Send a request and return the matching response frame.

        The protocol is pure request/response: one frame in, one frame out.
        """
        obj.setdefault("id", self._next_id())
        self._send(obj)
        return self._read_frame()

    def raw_frame(self, body):
        """Send a raw JSON body (str) as a length-prefixed frame and return the
        parsed response, without auto-assigning an id.

        Exercises malformed frames the typed helpers cannot build (nested or
        duplicate keys, negative/fractional numbers, trailing bytes); the caller
        asserts on the returned dict rather than expecting ok=true.
        """
        data = body.encode("utf-8")
        self.ser.write(struct.pack("<I", len(data)) + data)
        self.ser.flush()
        return self._read_frame()

    def _expect_ok(self, resp):
        if not resp.get("ok"):
            raise DeviceError(resp.get("error", "unknown error"), json.dumps(resp))
        return resp

    # ── message families ─────────────────────────────────────────
    def capabilities(self):
        return self._expect_ok(self.request({"type": "capabilities"}))

    def state(self):
        return self._expect_ok(self.request({"type": "state"}))

    def config_get(self):
        return self._expect_ok(self.request({"type": "config"}))

    def config_set(self, **fields):
        return self._expect_ok(self.request({"type": "config", **fields}))

    def control(self, **fields):
        return self._expect_ok(self.request({"type": "control", **fields}))

    def action(self, name, **params):
        obj = {"type": "action", "action": name}
        if params:
            obj.update(params)
        return self._expect_ok(self.request(obj))
