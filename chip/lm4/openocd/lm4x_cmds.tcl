# Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Command automation for Blizzard LM4F chip

# Program internal flash

proc flash_lm4 {path offset} {
	#set firstsect [expr {$offset / 1024}];
	#set lastsect [expr {($offset + $size) / 1024 - 1}];
	reset halt;
	flash write_image erase $path $offset;
	reset
}

# Link proto0 has 128KB flash; proto1+ have 256KB
proc flash_link { } {
	flash_lm4 ../../../build/link/ec.bin 0
}

proc flash_link_ro { } {
	flash_lm4 ../../../build/link/ec.RO.flat 0
}

proc flash_link_rw { } {
	flash_lm4 ../../../build/link/ec.RW.bin 81920
}

proc flash_bds { } {
	flash_lm4 ../../../build/bds/ec.bin 0
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

proc flash_emerged_link { } {
	set firmware_image ../../../../../../chroot/build/link/firmware/ec.bin

	flash_lm4 $firmware_image 0
}
