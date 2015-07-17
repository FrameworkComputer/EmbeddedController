# Copyright 2015 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Command automation for Nordic nRF51 chip

proc flash_nrf51 {path offset} {
	reset halt;
	program $path $offset;
}

proc unprotect_nrf51 { } {
	reset halt;
	nrf51 mass_erase;
}

# enable reset by writing 1 to the RESET register
# This will disconnect the debugger with the following message:
# Polling target nrf51.cpu failed, trying to reexamine
proc exit_debug_mode_nrf51 { } {
	mww 0x40000544 1;
}
