# Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

#
# Simple flashing command to reflash the discovery board using openocd
#
.PHONY: flash

flash:
	sudo openocd -f board/stm32f0discovery.cfg \
		-c "gdb_port 0" \
		-c "tcl_port 0" \
		-c "telnet_port 0" \
		-c "init" \
		-c "reset init" \
		-c "flash write_image erase ../../build/discovery-stm32f072/ec.bin 0x08000000" \
		-c "reset halt" \
		-c "resume" \
		-c "shutdown"
