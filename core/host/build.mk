# -*- makefile -*-
# Copyright 2013 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# emulator specific files build
#

# Build host board in 32-bit mode for a better chance of catching things like
# 64-bit write tearing.
CFLAGS_CPU=-fno-builtin -m32

core-y=main.o task.o timer.o panic.o disabled.o stack_trace.o
