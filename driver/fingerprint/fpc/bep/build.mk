# Copyright 2019 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# FPC BEP source files build

# Note that this variable includes the trailing "/"
_bep_cur_dir:=$(dir $(lastword $(MAKEFILE_LIST)))

# Make sure output directory is created (in build directory)
dirs-y+="$(_bep_cur_dir)"

# Only build for these objects for the RW image
all-obj-rw+=$(_bep_cur_dir)fpc_misc.o \
	$(_bep_cur_dir)fpc_private.o \
	$(_bep_cur_dir)fpc_sensor_spi.o \
	$(_bep_cur_dir)fpc_timebase.o
