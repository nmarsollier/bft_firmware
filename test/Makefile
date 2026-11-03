# BFT — test suite runner
#
#   make flash         flash the firmware
#   make test-cdc      serial protocol (CDC-ACM)

PORT   ?= /dev/cu.usbserial-1410      # USB-Serial/JTAG (flash/monitor)
SERIAL ?=                               # CDC-ACM port — auto-detected when empty

.PHONY: flash test-cdc

# Flash the firmware as a test build: separate build dir (build.test) + sdkconfig.test
# (CONFIG_BFT_TEST_BUILD=y, external-power gating disabled) so the suite runs over USB
# without the 12V rail.  The production build/ and sdkconfig are left untouched, so a
# later `idf.py build` still flashes with the safety gate enabled.
flash:
	cd .. && source ~/.espressif/v6.0.1/esp-idf/export.sh && idf.py -B build.test -DSDKCONFIG=sdkconfig.test -p $(PORT) flash

# Serial protocol suite.
test-cdc:
	@echo "== Serial protocol (CDC-ACM) =="
	@cd proto && NRM_SERIAL=$(SERIAL) python3 -m pytest -q
