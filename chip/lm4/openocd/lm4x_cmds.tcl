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

proc flash_link { } {
	flash_lm4 ../../../build/link/ec.bin 0
}

proc flash_link_ro { } {
	flash_lm4 ../../../build/link/ec.RO.flat 0
}

# Link originally had 80KB images, but TOT builds 128KB images
proc flash_link_rw { } {
	flash_lm4 ../../../build/link/ec.RW.bin 131072
}

proc flash_bds { } {
	flash_lm4 ../../../build/bds/ec.bin 0
}

proc flash_slippy { } {
	flash_lm4 ../../../build/slippy/ec.bin 0
}

# Bolt/slippy/falco/peppy/wolf have 128KB images
proc flash_bolt { } {
	flash_lm4 ../../../build/bolt/ec.bin 0
}

proc flash_bolt_ro { } {
	flash_lm4 ../../../build/bolt/ec.RO.flat 0
}

proc flash_bolt_rw { } {
	flash_lm4 ../../../build/bolt/ec.RW.bin 131072
}

proc flash_slippy_rw { } {
	flash_lm4 ../../../build/slippy/ec.RW.bin 131072
}

proc flash_falco { } {
	flash_lm4 ../../../build/falco/ec.bin 0
}

proc flash_peppy { } {
	flash_lm4 ../../../build/peppy/ec.bin 0
}

proc flash_wolf { } {
	flash_lm4 ../../../build/wolf/ec.bin 0
}

# link has pstate in last sector
proc unprotect_link { } {
	reset halt
	flash erase_sector 0 254 255
	reset
}

# Slippy/peppy/falco/wolf have pstate following RO
proc unprotect_slippy { } {
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
