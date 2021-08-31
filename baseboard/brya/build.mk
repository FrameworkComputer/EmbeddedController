# -*- makefile -*-
# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Brya baseboard specific files build
#

baseboard-y=
baseboard-y+=baseboard.o
baseboard-y+=battery_presence.o
baseboard-y+=cbi.o
baseboard-y+=usb_pd_policy.o
