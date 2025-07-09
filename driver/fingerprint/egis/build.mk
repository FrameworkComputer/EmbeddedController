# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Build for EGIS fingerprint library

_egis_cur_dir:=$(dir $(lastword $(MAKEFILE_LIST)))

ifneq (,$(filter rw,$(CONFIG_FP_SENSOR_EGIS630)))

# Make sure output directory is created (in build directory)
dirs-y+="$(_egis_cur_dir)"
all-obj-rw+=$(_egis_cur_dir)egis_private.o
all-obj-rw+=$(_egis_cur_dir)platform/src/plat_spi_mcu.o
all-obj-rw+=$(_egis_cur_dir)platform/src/plat_log_mcu.o
all-obj-rw+=$(_egis_cur_dir)platform/src/plat_time_mcu.o
all-obj-rw+=$(_egis_cur_dir)platform/src/plat_mem_mcu.o
all-obj-rw+=$(_egis_cur_dir)platform/src/plat_reset_mcu.o

endif
