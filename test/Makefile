# NRM-BFM — test suite runner
#
#   make flash         flash the firmware
#   make test-cdc      serial protocol (CDC-ACM)

PORT   ?= /dev/cu.usbserial-1410      # USB-Serial/JTAG (flash/monitor)
SERIAL ?=                               # CDC-ACM port — auto-detected when empty

.PHONY: flash test-cdc

# Flash the firmware.
flash:
	cd .. && source ~/.espressif/v6.0.1/esp-idf/export.sh && idf.py -p $(PORT) flash

# Serial protocol suite.
test-cdc:
	@echo "== Serial protocol (CDC-ACM) =="
	@cd proto && NRM_SERIAL=$(SERIAL) python3 -m pytest -q
