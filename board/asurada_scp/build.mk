# -*- makefile -*-
# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build
#

CHIP:=mt8192_scp
CHIP_VARIANT:=mt8192

board-y=board.o
board-$(HAS_TASK_VDEC_SERVICE)+=vdec.o
board-$(HAS_TASK_VENC_SERVICE)+=venc.o

# MDP3
board-$(HAS_TASK_MDP_SERVICE)+=mdp.o
