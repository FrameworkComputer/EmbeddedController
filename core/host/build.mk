# -*- makefile -*-
# Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# emulator specific files build
#

CFLAGS_CPU=-fno-builtin -m32

core-y=main.o task.o timer.o panic.o disabled.o
