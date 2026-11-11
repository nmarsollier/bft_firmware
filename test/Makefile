# BFT — test suite runner
#
#   make flash         flash the firmware
#   make test-cdc      serial protocol (CDC-ACM)

# PORT auto-detects the board's USB-Serial/JTAG endpoint unless overridden
# (e.g. `make -C test flash PORT=/dev/cu.usbserial-XXXX`).  SERIAL is the
# CDC-ACM protocol port, auto-detected by the pytest fixture when left empty.
PORT   ?= $(shell cd proto && python3 -c "from client import detect_flash_port; print(detect_flash_port() or '')")
SERIAL ?=

.PHONY: flash test-cdc

# Flash the firmware as a test build: separate build dir (build.test) + sdkconfig.test.
# The production build/ and sdkconfig are left untouched.  Note: motion requires the
# 12V rail so the TMC2209s answer their UART probe ("TMC working" gate).
flash:
	@test -n "$(PORT)" || { echo "No BFT USB-Serial/JTAG port found — is the board connected and in boot mode? (override with PORT=/dev/cu.usbserial-XXXX)"; exit 1; }
	cd .. && source ~/.espressif/v6.0.1/esp-idf/export.sh && idf.py -B build.test -DSDKCONFIG=sdkconfig.test -p $(PORT) flash

# Serial protocol suite.
test-cdc:
	@echo "== Serial protocol (CDC-ACM) =="
	@cd proto && NRM_SERIAL=$(SERIAL) python3 -m pytest -q
