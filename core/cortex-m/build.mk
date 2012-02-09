# Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Cortex-M4 core OS files build
#

# CPU specific compilation flags
CFLAGS_CPU=-mcpu=cortex-m4 -mthumb -Os -mno-sched-prolog

core-y=init.o panic.o switch.o task.o timer.o
