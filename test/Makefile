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

# Flash the firmware as a test build: separate build dir (build.test) + sdkconfig.test
# (CONFIG_BFT_TEST_BUILD=y, external-power gating disabled) so the suite runs over USB
# without the 12V rail.  The production build/ and sdkconfig are left untouched, so a
# later `idf.py build` still flashes with the safety gate enabled.
flash:
	@test -n "$(PORT)" || { echo "No BFT USB-Serial/JTAG port found — is the board connected and in boot mode? (override with PORT=/dev/cu.usbserial-XXXX)"; exit 1; }
	cd .. && source ~/.espressif/v6.0.1/esp-idf/export.sh && idf.py -B build.test -DSDKCONFIG=sdkconfig.test -p $(PORT) flash

# Serial protocol suite.
test-cdc:
	@echo "== Serial protocol (CDC-ACM) =="
	@cd proto && NRM_SERIAL=$(SERIAL) python3 -m pytest -q
