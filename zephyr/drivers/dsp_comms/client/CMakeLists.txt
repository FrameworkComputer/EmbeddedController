# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

include(${ZEPHYR_PIGWEED_MODULE_DIR}/pw_build/pigweed.cmake)

pw_add_library(dsp.client STATIC
  SOURCES
    dsp_client.c
    $<$<BOOL:${CONFIG_PLATFORM_EC_DSP_REMOTE_CBI}>:remote_cbi.c>
    $<$<BOOL:${CONFIG_PLATFORM_EC_DSP_REMOTE_LID_ANGLE}>:remote_lid_angle.c>
    $<$<BOOL:${CONFIG_PLATFORM_EC_DSP_REMOTE_LID_SWITCH}>:remote_lid_switch.c>
    $<$<BOOL:${CONFIG_PLATFORM_EC_DSP_REMOTE_TABLET_SWITCH}>:remote_tablet_switch.c>
  HEADERS
    include/cros/dsp/client.h
  PUBLIC_INCLUDES
    include
  PRIVATE_DEPS
    zephyr_interface
    cros_ec_interface
    ec_shim
)
if(CONFIG_PLATFORM_EC_DSP_CLIENT)
  zephyr_append_cmake_library(dsp.client)
endif()
