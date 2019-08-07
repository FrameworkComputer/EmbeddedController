# -*- makefile -*-
# Copyright 2018 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build
#
CHIP:=mt_scp
CHIP_VARIANT:=mt8183

board-y=board.o
board-$(HAS_TASK_VDEC_SERVICE)+=vdec.o
board-$(HAS_TASK_VENC_SERVICE)+=venc.o

# ISP P1
board-$(HAS_TASK_ISP_SERVICE)+=isp_p1_srv.o
# FD
board-$(HAS_TASK_FD_SERVICE)+=fd.o

# ISP P2
board-$(HAS_TASK_DIP_SERVICE)+=isp_p2_srv.o
# MDP3
board-$(HAS_TASK_MDP_SERVICE)+=mdp_ipi_message.o
