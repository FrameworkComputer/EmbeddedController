# Copyright 2022 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Register zmake project for ap_power test."""

register_host_test("ap_power", dts_overlays=["overlay.dts"])
