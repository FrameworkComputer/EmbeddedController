# -*- makefile -*-
# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Baseboard specific files build
#
baseboard-y+=baseboard.o

baseboard-$(HAS_TASK_VDEC_SERVICE)+=vdec.o
baseboard-$(HAS_TASK_VENC_SERVICE)+=venc.o
baseboard-$(HAS_TASK_MDP_SERVICE)+=mdp.o
