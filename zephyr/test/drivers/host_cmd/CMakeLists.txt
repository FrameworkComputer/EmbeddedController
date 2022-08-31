# Copyright 2022 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

target_sources(app PRIVATE
    src/battery_cut_off.c
    src/get_panic_info.c
    src/get_pd_port_caps.c
    src/host_event_commands.c
    src/host_event_commands_deprecated.c
    src/keyboard_mkbp.c
    src/motion_sense.c
    src/pd_control.c
    src/pd_chip_info.c
    src/pd_log.c
    src/usb_pd_control.c
)
