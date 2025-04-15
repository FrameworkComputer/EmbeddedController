# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

include(${ZEPHYR_PIGWEED_MODULE_DIR}/pw_build/pigweed.cmake)

pw_add_library(dsp.service STATIC
  SOURCES
    dsp_service.cc
    i2c_target_pio_funcs.cc
    $<$<BOOL:${CONFIG_I2C_TARGET_BUFFER_MODE}>:i2c_target_buffer_funcs.cc>
    $<$<BOOL:${CONFIG_PLATFORM_EC_DSP_REMOTE_BODY_DETECTION}>:remote_body_detection.cc>
  HEADERS
    include/cros/dsp/service/cros_transport.hh
    include/cros/dsp/service/driver.hh
  PRIVATE_INCLUDES
    include
  PUBLIC_DEPS
    pw_assert
    pw_bytes
    pw_function
    pw_polyfill
    pw_preprocessor
    pw_result
    pw_status
    pw_transport.service
    cros_ec_interface
)
if(CONFIG_PLATFORM_EC_DSP_SERVICE)
  zephyr_append_cmake_library(dsp.service)
endif()

target_sources_ifdef(CONFIG_PLATFORM_EC_DSP_REMOTE_LID_ANGLE
  ec_shim PRIVATE "${PLATFORM_EC}/common/lid_angle_common.c"
)
