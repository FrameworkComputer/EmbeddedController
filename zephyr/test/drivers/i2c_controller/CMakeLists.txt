# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include")

target_sources(app PRIVATE
  src/basic_i2c_device_emul.c
  src/i2c_controller.c
)
