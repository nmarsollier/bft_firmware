# Big Fucking Mount

Fork equatorial mount for astrophotography: the telescope sits balanced between
the fork arms, with no counterweight and no meridian flip. An ESP32-S3 drives
both axes over USB CDC-ACM; a Go gateway translates that protocol to
Alpaca/ASCOM for N.I.N.A. and PHD2.

## Sibling projects

- **`~/Dev/eaf_firmware`** — electronic focuser + accessories firmware (same
  serial protocol, `role="accessories"`): focuser motor, primary-mirror fan,
  mirror/ambient temperature, humidity and vibration sensing.
- **`~/Dev/mount_panel`** — Go gateway (Alpaca/UI): the desktop client that
  connects to the mount and the EAF and exposes them to N.I.N.A. and PHD2.

## Build & flash

Requirements: ESP-IDF v6.0.1, Python 3.10+ (venv), CMake 4.x, Ninja 1.x.

```sh
idf.py set-target esp32s3   # once
idf.py build                # build the firmware
idf.py -p PORT flash        # flash the board
idf.py -p PORT monitor      # serial console (logs)
```

Toolchain install (macOS, one-time):

```sh
mkdir -p ~/.espressif
git clone --depth 1 --branch v6.0.1 https://github.com/espressif/esp-idf.git ~/.espressif/v6.0.1/esp-idf
cd ~/.espressif/v6.0.1/esp-idf && ./install.sh esp32s3
brew install cmake ninja
```

## Run

The firmware has no UI of its own: it only answers over the USB CDC-ACM port.
The client is the Go gateway (`~/Dev/mount_panel`), which exposes Alpaca/ASCOM and
the UI for N.I.N.A. and PHD2.

## Tests

```sh
make test-cdc     # serial protocol suite (requires the board connected over USB)
make flash        # shortcut: build + flash
```

## Architecture

```
Desktop gateway (Go) — Alpaca + UI
  └─ USB CDC-ACM (serial protocol)
       └─ ESP32-S3: Proto → Mount → Motors → STEP/DIR (RMT)
```

- **usb_cdc** (`main/usb_cdc/`) — CDC-ACM transport (TinyUSB).
- **proto** (`main/proto/`) — serial protocol: framing, dispatch, one handler per family.
- **mount** (`main/mount/`) — orchestration: state, coordinates, config.
- **motors** (`main/motors/`) — motion: STEP/DIR GPIO and RMT pulse generation.
- **power** (`main/power/`) — external power sensing.
- **led** (`main/led/`) / **buzzer** (`main/buzzer/`) — status indicator and beeps.
- **runtime** (`main/runtime/`) — init sequence + periodic loop.

## Where the details live

- **Pinout, microstepping and reduction**: `main/config/config.h` (single source of truth).
- **Coordinate model**: `MOUNT.md`.
- **Motors and drivers**: `MOTOR.md`.
- **Serial protocol**: `main/proto/README.md`.
