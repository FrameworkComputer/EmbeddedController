# -*- makefile -*-
# Copyright 2018 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# fuzzer binaries
#

fuzz-test-list-host =
# Fuzzers should only be built for architectures that support sanitizers.
ifeq ($(ARCH),amd64)
fuzz-test-list-host += host_command_fuzz usb_pd_fuzz usb_tcpm_v2_rev20_fuzz \
	usb_tcpm_v2_rev30_fuzz
endif

# For fuzzing targets libec.a is built from the ro objects and hides functions
# that collide with stdlib. The rw only objects are then linked against libec.a
# with stdlib support. Therefore fuzzing targets that need to call this internal
# functions should be marked "-y" or "-ro", and fuzzing targets that need stdlib
# should be marked "-rw". In other words:
#
# Does your object file need to link against the Cr50 implementations of stdlib
# functions?
#   Yes -> use <obj_name>-y
# Does your object file need to link against cstdlib?
#   Yes -> use <obj_name>-rw
# Otherwise use <obj_name>-y
host_command_fuzz-y = host_command_fuzz.o
usb_pd_fuzz-y = usb_pd_fuzz.o
usb_tcpm_v2_rev30_fuzz-y = usb_pd_fuzz.o usb_tcpm_v2_rev30_fuzz.o \
	../test/fake_battery.o
usb_tcpm_v2_rev20_fuzz-y = usb_pd_fuzz.o usb_tcpm_v2_rev20_fuzz.o \
	../test/fake_battery.o
