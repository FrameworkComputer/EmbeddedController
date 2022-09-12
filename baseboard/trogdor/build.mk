# -*- makefile -*-
# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Baseboard specific files build
#

baseboard-y+=baseboard.o
baseboard-y+=hibernate.o
baseboard-y+=power.o
baseboard-y+=usb_pd_policy.o
