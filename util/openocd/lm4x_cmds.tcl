# Copyright 2012 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Command automation for Blizzard LM4F chip

# Program internal flash

proc flash_lm4 {path offset} {
	reset halt;
	flash write_image erase $path $offset;
	reset
}

proc flash_lm4_board {board} {
	flash_lm4 ../../build/$board/ec.bin 0
}

proc flash_lm4_ro {board} {
	flash_lm4 ../../build/$board/RO/ec.RO.flat 0
}

proc flash_lm4_rw {board} {
	flash_lm4 ../../build/$board/RW/ec.RW.bin 131072
}

# Boards with CONFIG_FLASH_PSTATE_BANK have pstate following RO
proc unprotect_pstate { } {
	reset halt
	flash erase_sector 0 126 127
	reset
}

# Boot a software using internal RAM only

proc ramboot_lm4 {path} {
	reset halt
	load_image $path 0x20000000 bin
	reg 15 0x20000400
	resume
}

proc ramboot_lm4_board {board} {
	ramboot_lm4 ../../../build/$board/ec.RO.flat
}

proc flash_emerged_board {board} {
	set firmware_image ../../../../../../chroot/build/$board/firmware/ec.bin

	flash_lm4 $firmware_image 0
}
