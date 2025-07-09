# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# TODO(b/345530593) Update the DSP communication to use pw_rpc once Pigweed is
#     unpinned and supports Zephyr I2C/GPIO abstractions.

include(${ZEPHYR_PIGWEED_MODULE_DIR}/pw_build/pigweed.cmake)
include(${ZEPHYR_PIGWEED_MODULE_DIR}/pw_protobuf_compiler/proto.cmake)

set(pw_protobuf_compiler_GENERATE_PROTOS_ARGS "--no-experimental-editions" CACHE STRING "Args to generate_protos.py" FORCE)

pw_proto_library(dsp_comms.proto
  SOURCES
    proto/ec_dsp.proto
  INPUTS
    proto/ec_dsp.options
  DEPS
    pw_third_party.nanopb.proto
)

zephyr_link_libraries(dsp_comms.proto.nanopb pw_transport.proto.nanopb)

add_subdirectory(service)
add_subdirectory(client)

pw_add_test(cros.dsp.test
  SOURCES
    test_comms.cc
  PRIVATE_DEPS
    pw_unit_test.light
    pw_unit_test.logging_event_handler
    pw_assert
    dsp_comms.proto.nanopb
    dsp.client
    dsp.service
    zephyr_interface
    cros_ec_interface
)
