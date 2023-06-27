# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

target_sources(app
  PRIVATE
    src/suite.c
    ${PLATFORM_EC}/zephyr/shim/src/system.c
)

target_include_directories(app PRIVATE include)

dt_has_chosen(has_bbram PROPERTY "cros-ec,bbram")
if(has_bbram)
  target_sources(app PRIVATE src/test_system.c)
else()
  target_sources(app PRIVATE src/no_chosen.c)
endif()
