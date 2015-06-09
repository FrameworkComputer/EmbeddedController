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

proc flash_auron { } {
	flash_lm4 ../../../build/auron/ec.bin 0
}

proc flash_bds { } {
	flash_lm4 ../../../build/bds/ec.bin 0
}

proc flash_rambi { } {
	flash_lm4 ../../../build/rambi/ec.bin 0
}

proc flash_samus { } {
	flash_lm4 ../../../build/samus/ec.bin 0
}

proc flash_samus_ro { } {
	flash_lm4 ../../../build/samus/ec.RO.flat 0
}

proc flash_samus_rw { } {
	flash_lm4 ../../../build/samus/ec.RW.bin 131072
}

proc flash_rambi_ro { } {
	flash_lm4 ../../../build/rambi/ec.RO.flat 0
}

proc flash_rambi_rw { } {
	flash_lm4 ../../../build/rambi/ec.RW.bin 131072
}

# Auron have pstate following RO
proc unprotect_auron { } {
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

proc ramboot_bds { } {
	ramboot_lm4 ../../../build/bds/ec.RO.flat
}
