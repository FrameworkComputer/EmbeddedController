# -*- makefile -*-
# Copyright 2013 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# emulator specific files build
#

CFLAGS_CPU=-fno-builtin

core-y=main.o task.o timer.o panic.o disabled.o stack_trace.o
