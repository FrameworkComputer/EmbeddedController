#!/bin/bash
# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This is an example script showing how to compare configs on hayato

# Usage of this script:
# - set the three variables below
# - set up the ecos.conf3 file as follows:
#   $ make print-configs BOARD=hayato >econ.conf
#   $ grep CONFIG ecos.conf |sort |uniq >ecos.conf2
#   $ cat ecos.conf2  |awk '{print $1}' >ecos.conf3
# - configure and build hayato for zephyr
#   $ zmake configure -B /tmp/z/hay zephyr/projects/asurada/hayato/ -t zephyr -b
# - run this script to check for CONFIG mismatches
#   $ zephyr/get_cfg.sh

set -e

dir=/tmp/z/hay
subdir=build-singleimage
in=~/cosarm/src/platform/ec/ecos.conf3

cd "${dir}"
pushd "${subdir}"

# This is created by running:
#
#    ninja -v -C /tmp/z/hay/build-singleimage
#
# then replacing '-C' by '-E -dM'
# The idea is to get a list of the #defines used to compile the code
ccache /opt/zephyr-sdk/riscv64-zephyr-elf/bin/riscv64-zephyr-elf-gcc -DBUILD_VERSION=zephyr-v2.5.0-101-g7a99b4a3ee12 -DCHROMIUM_EC -DCONFIG_ZEPHYR -DKERNEL -D_FORTIFY_SOURCE=2 -D__ZEPHYR__=1 -I/scratch/sglass/cosarm/src/platform/ec/zephyr/include -I/tmp/z/hay/modules/ec/zephyr/shim/include -I/tmp/z/hay/modules/ec/fuzz -I/tmp/z/hay/modules/ec/test -I/tmp/z/hay/modules/ec/include -I/tmp/z/hay/modules/ec/include/driver -I/tmp/z/hay/modules/ec/third_party -I/scratch/sglass/cosarm/src/platform/ec/zephyr/app/ec/include -I/scratch/sglass/cosarm/src/platform/ec/zephyr/shim/chip/it8xxx2/include -I/scratch/sglass/cosarm/src/third_party/zephyr/main/v2.5/include -Izephyr/include/generated -I/scratch/sglass/cosarm/src/third_party/zephyr/main/v2.5/soc/riscv/riscv-ite/it8xxx2 -I/scratch/sglass/cosarm/src/third_party/zephyr/main/v2.5/soc/riscv/riscv-ite/common/. -Iec/include/generated -I/scratch/sglass/cosarm/src/platform/ec/zephyr/projects/asurada/hayato/include -isystem /scratch/sglass/cosarm/src/third_party/zephyr/main/v2.5/lib/libc/minimal/include -isystem /opt/zephyr-sdk/riscv64-zephyr-elf/bin/../lib/gcc/riscv64-zephyr-elf/9.2.0/include -isystem /opt/zephyr-sdk/riscv64-zephyr-elf/bin/../lib/gcc/riscv64-zephyr-elf/9.2.0/include-fixed -Os -imacros /tmp/z/hay/build-singleimage/zephyr/include/generated/autoconf.h -ffreestanding -fno-common -g -mabi=ilp32 -march=rv32imac -imacros /scratch/sglass/cosarm/src/third_party/zephyr/main/v2.5/include/toolchain/zephyr_stdint.h -Wall -Wformat -Wformat-security -Wno-format-zero-length -Wno-main -Wno-pointer-sign -Wpointer-arith -Wno-address-of-packed-member -Wno-unused-but-set-variable -Werror=implicit-int -fno-asynchronous-unwind-tables -fno-pie -fno-pic -fno-strict-overflow -fno-reorder-functions -fno-defer-pop -fmacro-prefix-map=/scratch/sglass/cosarm/src/platform/ec/zephyr/projects/asurada/hayato=CMAKE_SOURCE_DIR -fmacro-prefix-map=/scratch/sglass/cosarm/src/third_party/zephyr/main/v2.5=ZEPHYR_BASE -ffunction-sections -fdata-sections -march=rv32i -std=c99 -nostdinc -MD -MT CMakeFiles/app.dir/tmp/z/hay/modules/ec/common/virtual_battery.c.obj -MF CMakeFiles/app.dir/tmp/z/hay/modules/ec/common/virtual_battery.c.obj.d \
	-o /tmp/z/hay/zephyr.conf -E -dM \
	/tmp/z/hay/modules/ec/common/virtual_battery.c
popd

sort <zephyr.conf >zephyr.conf2

# Only look at CONFIG_xxx where xxx does not start with PLATFORM_EC
grep <zephyr.conf2 -P 'CONFIG_(?!PLATFORM_EC)[A-Z]+' | \
	awk '{print $2}' | \
	sort >zephyr.conf3
comm -23 "${in}" zephyr.conf3
