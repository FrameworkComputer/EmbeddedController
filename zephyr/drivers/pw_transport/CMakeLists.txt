# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

include(${ZEPHYR_PIGWEED_MODULE_DIR}/pw_build/pigweed.cmake)
include(${ZEPHYR_PIGWEED_MODULE_DIR}/pw_protobuf_compiler/proto.cmake)

set(PW_TRANSPORT_STATUS_FLAGS_SIZE 1 CACHE STRING "")

configure_file(proto/transport.options.in proto/transport.options @ONLY)
file(
  COPY
    "${CMAKE_CURRENT_SOURCE_DIR}/proto/transport.proto"
  DESTINATION
    "${CMAKE_CURRENT_BINARY_DIR}/proto"
)

if(NOT DEFINED CONFIG_NANOPB)
  message(DEBUG "pw_transport requires nanopb for now")
  return()
endif()

pw_proto_library(pw_transport.proto
  SOURCES
    "${CMAKE_CURRENT_BINARY_DIR}/proto/transport.proto"
  INPUTS
    "${CMAKE_CURRENT_BINARY_DIR}/proto/transport.options"
  STRIP_PREFIX
    "${CMAKE_CURRENT_BINARY_DIR}"
  PREFIX
    "pw_transport"
  DEPS
    pw_third_party.nanopb.proto
)

pw_add_library(pw_transport.service INTERFACE
  HEADERS
    public/pw_transport/service.h
  PUBLIC_INCLUDES
    public
  PUBLIC_DEPS
    pw_fsm.fsm
    pw_transport.proto.nanopb
    pw_function
    pw_bytes
    pw_polyfill
    pw_status
    pw_result
)

pw_add_test(pw_transport.service_test
  SOURCES
    test_transport_service.cc
  PRIVATE_DEPS
    pw_transport.service
    pw_unit_test.light
    pw_unit_test.logging_event_handler
    zephyr_interface
    cros_ec_interface
    pw_function
)
