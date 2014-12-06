# Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Command automation for NPCX5M5G chip

# Program spi flash
source [find mem_helper.tcl]

proc flash_npcx {image_path image_offset image_size spifw_image} {
	set UPLOAD_FLAG 0x200C4000;

	# Upload program spi image FW to lower 16KB Data RAM
	fast_load_image $spifw_image 0x200C0000
	fast_load
	# Clear whole 128KB Code RAM
	mwb 0x10088000 0xFF 0x20000
	# Upload binary image to Code RAM
	fast_load_image $image_path 0x10088000
	fast_load
	# Set sp to upper 16KB Data RAM
	reg sp 0x200C8000
	# Set spi offset address of uploaded image
	reg r0 $image_offset
	# Set spi program size of uploaded image
	reg r1 $image_size
	# Clear upload flag
	mww $UPLOAD_FLAG 0x0
	# Start to program spi flash
	resume 0x200C0001
	echo "*** Program ...  ***"
	sleep 1
	# Wait for any pending flash operations to complete
	while {[expr [mrw $UPLOAD_FLAG] & 0x01] == 0} { sleep 1 }

	# Halt CPU
	halt

	if {[expr [mrw $UPLOAD_FLAG] & 0x02] == 0} {
		echo "*** Program Fail ***"
	} else {
		echo "*** Program Done ***"
	}

}

proc flash_npcx_ro {image_offset} {
	# 128 KB for RO& RW regions
	set fw_size  0x20000
	# images path
	set outdir ../../../build/npcx_evb
	set ro_image_path $outdir/ec.RO.flat
	set spifw_image	$outdir/chip/npcx/spiflashfw/ec_npcxflash.bin

	# Halt CPU first
	halt
	echo "*** Start to program RO region ***"
	# Write to lower 128kB from offset
	flash_npcx $ro_image_path $image_offset $fw_size $spifw_image
	echo "*** Finish program RO region ***"

	# Reset CPU
	reset
}

proc flash_npcx_evb {image_offset} {
	# 128 KB for RO& RW regions
	set fw_size  0x20000
	# 4K little FW
	set lfw_size 0x1000
	# 8M spi-flash
	set flash_size 0x800000

	# images path
	set outdir ../../../build/npcx_evb
	set ro_image_path $outdir/ec.RO.flat
	set rw_image_path $outdir/ec.RW.bin
	set lfw_image_path $outdir/chip/npcx/lfw/ec_lfw.bin
	set spifw_image	$outdir/chip/npcx/spiflashfw/ec_npcxflash.bin

	# images offset
	set rw_image_offset  [expr ($image_offset + $fw_size)]
	set lfw_image_offset [expr ($flash_size - $lfw_size)]

	# Halt CPU first
	halt
	echo "*** Start to program RO region ***"
	# Write to lower 128kB from offset
	flash_npcx $ro_image_path $image_offset $fw_size $spifw_image
	echo "*** Finish program RO region ***"

	echo "*** Start to program RW region ***"
	# Write to upper 128kB from offset
	flash_npcx $rw_image_path $rw_image_offset $fw_size $spifw_image
	echo "*** Finish program RW region ***"

	echo "*** Start to program LFW region ***"
	# Write to top of flash minus 4KB
	flash_npcx $lfw_image_path $lfw_image_offset $lfw_size $spifw_image
	echo "*** Finish program LFW region ***"

	# Reset CPU
	reset
}
