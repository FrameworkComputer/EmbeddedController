# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

include($ENV{PW_ROOT}/pw_build/pigweed.cmake)
include($ENV{PW_ROOT}/pw_protobuf_compiler/proto.cmake)

pw_proto_library(dsp_comms.proto
  SOURCES
    proto/ec_dsp.proto
  INPUTS
    proto/ec_dsp.options
  DEPS
    pw_third_party.nanopb.proto
)
