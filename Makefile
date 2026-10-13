# NRM-BFM — top-level convenience Makefile.
#
# Forwards the test/flash targets to test/Makefile so they can be invoked from
# the project root without `cd test`:
#
#   make test-cdc      serial protocol suite
#   make flash         flash the firmware
#
# The firmware itself is built with idf.py (CMake), not make.

MAKEFLAGS += --no-print-directory

.PHONY: test-cdc flash

test-cdc flash:
	@$(MAKE) -C test $@
