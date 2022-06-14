# -*- makefile -*-
# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Baseboard specific files build
#
baseboard-y+=baseboard.o

baseboard-$(HAS_TASK_VDEC_SERVICE)+=vdec.o
baseboard-$(HAS_TASK_VENC_SERVICE)+=venc.o
baseboard-$(HAS_TASK_MDP_SERVICE)+=mdp.o
baseboard-$(HAS_TASK_CAM_SERVICE)+=cam.o
