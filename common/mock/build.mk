# Copyright 2019 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# See common/mock/README.md for more information.

mock-$(HAS_MOCK_FP_SENSOR) += fp_sensor_mock.o
mock-$(HAS_MOCK_FPSENSOR_DETECT) += fpsensor_detect_mock.o
mock-$(HAS_MOCK_FPSENSOR_STATE) += fpsensor_state_mock.o
mock-$(HAS_MOCK_MKBP_EVENTS) += mkbp_events_mock.o
mock-$(HAS_MOCK_ROLLBACK) += rollback_mock.o
mock-$(HAS_MOCK_TCPC) += tcpc_mock.o
mock-$(HAS_MOCK_TIMER) += timer_mock.o
mock-$(HAS_MOCK_USB_MUX) += usb_mux_mock.o
