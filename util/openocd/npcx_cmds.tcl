# Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Command automation for NPCX5M5G chip

# Program spi flash
source [find mem_helper.tcl]

proc flash_npcx {image_path image_offset image_size spifw_image} {
	set UPLOAD_FLAG 0x200C4000;

	echo "*** NPCX Reset and halt CPU first ***"
	reset halt

	# Clear whole 96KB Code RAM
	mwb 0x100A8000 0xFF 0x18000
	# Upload binary image to Code RAM
	load_image $image_path 0x100A8000

	# Upload program spi image FW to lower 16KB Data RAM
	load_image $spifw_image 0x200C0000

	# Set sp to upper 16KB Data RAM
	reg sp 0x200C8000
	# Set spi offset address of uploaded image
	reg r0 $image_offset
	# Set spi program size of uploaded image
	reg r1 $image_size
	# Set pc to start of spi program function
	reg pc 0x200C0001
	# Clear upload flag
	mww $UPLOAD_FLAG 0x0

	echo "*** Program ...  ***"
	# Start to program spi flash
	resume

	# Wait for any pending flash operations to complete
	while {[expr [mrw $UPLOAD_FLAG] & 0x01] == 0} { sleep 1000 }

	if {[expr [mrw $UPLOAD_FLAG] & 0x02] == 0} {
		echo "*** Program Fail ***"
	} else {
		echo "*** Program Done ***"
	}

	# Halt CPU
	halt
}

proc flash_npcx_ro {image_dir image_offset} {
	set MPU_RNR  0xE000ED98;
	set MPU_RASR 0xE000EDA0;

	# 96 KB for RO& RW regions
	set fw_size  0x18000
	# images path
	set ro_image_path $image_dir/ec.RO.flat
	set spifw_image	$image_dir/chip/npcx/spiflashfw/ec_npcxflash.bin

	# Halt CPU first
	halt

	# diable MPU for Data RAM
	mww $MPU_RNR  0x1
	mww $MPU_RASR 0x0

	echo "*** Start to program RO region ***"
	# Write to lower 96kB from offset
	flash_npcx $ro_image_path $image_offset $fw_size $spifw_image
	echo "*** Finish program RO region ***"

}

proc flash_npcx_all {image_dir image_offset} {
	set MPU_RNR  0xE000ED98;
	set MPU_RASR 0xE000EDA0;

	# 96 KB for RO& RW regions
	set fw_size  0x18000
	# 8M spi-flash
	set flash_size 0x800000

	# images path
	set ro_image_path $image_dir/RO/ec.RO.flat
	set rw_image_path $image_dir/RW/ec.RW.bin
	set spifw_image	$image_dir/chip/npcx/spiflashfw/ec_npcxflash.bin

	# images offset
	set rw_image_offset  [expr ($image_offset + 0x20000)]

	# Halt CPU first
	halt

	# diable MPU for Data RAM
	mww $MPU_RNR  0x1
	mww $MPU_RASR 0x0

	echo "*** Start to program RO region ***"
	# Write to lower 96kB from offset
	flash_npcx $ro_image_path $image_offset $fw_size $spifw_image
	echo "*** Finish program RO region ***\r\n"

	echo "*** Start to program RW region ***"
	# Write to upper 96kB from offset
	flash_npcx $rw_image_path $rw_image_offset $fw_size $spifw_image
	echo "*** Finish program RW region ***\r\n"

}

proc reset_halt_cpu { } {
	echo "*** NPCX Reset and halt CPU first ***"
	reset halt
}
