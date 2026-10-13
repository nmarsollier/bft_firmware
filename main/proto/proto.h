#pragma once

/*
 * Mount protocol — public entry point.
 *
 * The protocol is a length-prefixed JSON message stream carried over the
 * USB CDC-ACM transport.  It exposes the mount as five message families
 * (capabilities, state, config, control, action) so a desktop application can
 * translate them into ASCOM Alpaca and a UI.  See the plan document for the
 * full wire contract.
 *
 * proto_init() must be called after usb_cdc_init(): it wires the CDC receive
 * stream to the frame parser.
 */
void proto_init(void);
