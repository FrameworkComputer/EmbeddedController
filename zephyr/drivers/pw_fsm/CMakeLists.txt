# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

include(${ZEPHYR_PIGWEED_MODULE_DIR}/pw_build/pigweed.cmake)

pw_add_library(pw_fsm.fsm INTERFACE
  HEADERS
    public/pw_fsm/fsm.h
  PUBLIC_INCLUDES
    public
  PUBLIC_DEPS
    pw_status
    pw_containers
    pw_function
)

pw_add_test(pw_fsm.fsm_test
  SOURCES
    test_state_machine.cc
  PRIVATE_DEPS
    pw_fsm.fsm
    pw_unit_test.light
    pw_unit_test.logging_event_handler
)
