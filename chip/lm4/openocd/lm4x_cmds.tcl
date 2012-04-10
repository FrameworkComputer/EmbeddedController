# Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Command automation for Blizzard LM4F chip

# Program internal flash

proc flash_lm4 {path size} {
	set lastsect [expr {$size / 1024 - 1}];
	reset halt;
	flash erase_sector 0 0 $lastsect;
	# Note erase_sector silently fails sometimes; see crosbug.com/p/8632
	# Dump a few words as a diagnostic for whether erase succeeded
	mdw 0 16
	flash write_image $path 0;
	reset
}

# Link proto0 has 128KB flash; proto1+ have 256KB
proc flash_link { } {
	flash_lm4 ../../../build/link/ec.bin 262144
}

proc flash_bds { } {
	flash_lm4 ../../../build/bds/ec.bin 262144
}

# Boot a software using internal RAM only

proc ramboot_lm4 {path} {
	reset halt
	load_image $path 0x20000000 bin
	reg 15 0x20000400
	resume
}

proc ramboot_link { } {
	ramboot_lm4 ../../../build/link/ec.RO.flat
}

proc ramboot_bds { } {
	ramboot_lm4 ../../../build/bds/ec.RO.flat
}
