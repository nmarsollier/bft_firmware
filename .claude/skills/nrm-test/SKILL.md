---
name: nrm-test
description: Run the NRM-BFM mount test suite and fix any failure. Use when the user asks to run the tests, diagnose a failure, or fix a bug the suite surfaces.
---

# Run and fix the NRM-BFM tests

The NRM-BFM mount has a functional acceptance suite against real hardware, driven
through the **CDC-ACM serial protocol**.

- `test/proto/` → the serial protocol suite. Run with pytest.

## 1. Run the tests

```sh
make -C test test-cdc     # the serial protocol suite
```

Manual equivalent:

```sh
cd test/proto && python3 -m pytest -q
```

Useful pytest variants: `--lf` (only last failed), `-k "park"` (by name),
`test_got.py` (one family), `-x` (stop at first failure).

## 2. Prerequisites

- Board flashed with the firmware and connected over USB (CDC-ACM). The suite
  auto-detects the serial port by USB VID/PID.
- `pytest` installed (`python3 -m pip install pytest`).
- ESP-IDF v6.0.1 tools at `~/.espressif/v6.0.1/esp-idf` (`idf.py`).

## 3. If the board does not answer (serial down)

The CDC-ACM port on macOS can stay down after a flash/reset. Recover in this order:

```sh
# 1. clean SoC reset (forces USB re-enumeration)
source ~/.espressif/v6.0.1/esp-idf/export.sh
python -m esptool --chip esp32s3 -p /dev/cu.usbserial-1410 --after hard-reset chip-id

# 2. if still down, re-flash
idf.py -p /dev/cu.usbserial-1410 flash
```

Re-connect and confirm the serial port re-appears (`ls /dev/cu.usbmodem*`).

## 4. Diagnosing a failing test

1. **Read the exact message.** Distinguish:
   - `TransportError` → the board did not answer (see section 3).
   - `DeviceError: proto rejected: …` → the device answered `ok=false` (firmware bug).
   - `AssertionError` → the observed state/value does not match the expectation.
2. **Reproduce in isolation**: `python3 -m pytest test_xxx.py::test_name -v`.
3. **Confirm the real state** with a `state` read over the protocol (a small
   serial client, or the test's own `ProtoClient`).
4. **For crashes/resets** capture the serial console with DTR/RTS de-asserted:

```sh
python3 - <<'EOF'
import serial
s = serial.Serial("/dev/cu.usbserial-1410", 115200, timeout=0.2)
s.dtr = False; s.rts = False   # avoid the auto-program reset
while True:
    b = s.read(4096)
    if b: print(b.decode(errors="replace"), end="")
EOF
```

   Look for: `Guru Meditation Error`, `task_wdt`, `rst:` (reset cause), or
   `channel can't be disabled in state 0` (RMT).

## 5. Fixing the bug

- **Do not change the test expectation to make a bug pass.** The test expresses the
  correct behavior; if it fails, fix the firmware in `main/`.
- Fixes go in the matching domain (`main/proto/`, `main/usb_cdc/`, `main/motors/`,
  `main/mount/`, `main/power/`).
- After the fix: `make -C test flash` (rebuild + flash), wait for the board to come
  back, then re-run only the failing test, then the whole suite.

## 6. Conventions the suite respects

- **No `@pytest.mark.skip`.** What cannot be simulated is not in the suite.
- **No catalog codes** (VAL-01, GOT-02, D-08...). Tests are described with clear
  names and docstrings.
- The celestial position is validated with **spherical angular distance** (an
  independent oracle in `test/proto/oracle.py`), not a direct RA/DEC subtraction.
