# Copyright 2019 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# FPC BEP source files build

# Note that this variable includes the trailing "/"
_bep_cur_dir:=$(dir $(lastword $(MAKEFILE_LIST)))

# Make sure output directory is created (in build directory)
dirs-y+="$(_bep_cur_dir)"

sensor-$(CONFIG_FP_SENSOR_FPC1025)=fpc1025
sensor-$(CONFIG_FP_SENSOR_FPC1035)=fpc1035

# Only build for these objects for the RW image
all-obj-rw+=$(_bep_cur_dir)fpc_misc.o \
	$(_bep_cur_dir)fpc_private.o \
	$(_bep_cur_dir)fpc_sensor_spi.o \
	$(_bep_cur_dir)fpc_timebase.o
fp_sensor_header-rw=$(_bep_cur_dir)$(sensor-rw)_private.h

CPPFLAGS+=-DFP_SENSOR_PRIVATE=$(fp_sensor_header-rw)
