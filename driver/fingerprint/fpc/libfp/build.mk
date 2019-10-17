# Copyright 2019 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# FPC libfp source files build

# Note that this variable includes the trailing "/"
libfp_cur_dir:=$(dir $(lastword $(MAKEFILE_LIST)))

# Make sure output directory is created (in build directory)
dirs-y+="$(libfp_cur_dir)"

sensor-$(CONFIG_FP_SENSOR_FPC1145)=fpc1145

# Only build for these objects for the RW image
all-obj-rw+=$(libfp_cur_dir)fpc_sensor_pal.o \
			$(libfp_cur_dir)fpc_private.o
fp_sensor_header-rw=$(libfp_cur_dir)$(sensor-rw)_private.h

CPPFLAGS+=-DFP_SENSOR_PRIVATE=$(fp_sensor_header-rw)
CPPFLAGS+=-DFP_SENSOR_CONFIG=$(call uppercase,$(sensor-rw))
