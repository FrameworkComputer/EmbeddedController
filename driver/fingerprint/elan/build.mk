# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Build for ELAN fingerprint drivers

# Note that this variable includes the trailing "/"
_elan_cur_dir:=$(dir $(lastword $(MAKEFILE_LIST)))

ifneq (,$(filter rw,$(CONFIG_FP_SENSOR_ELAN80) $(CONFIG_FP_SENSOR_ELAN80SG) \
			$(CONFIG_FP_SENSOR_ELAN515)))

# Make sure output directory is created (in build directory)
dirs-y+="$(_elan_cur_dir)"

all-obj-rw+=$(_elan_cur_dir)elan_private.o
all-obj-rw+=$(_elan_cur_dir)elan_sensor_pal.o

endif
