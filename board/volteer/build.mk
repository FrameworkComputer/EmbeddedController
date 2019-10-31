# -*- makefile -*-
# Copyright 2019 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build
#

CHIP:=npcx
CHIP_FAMILY:=npcx7
CHIP_VARIANT:=npcx7m6fc
BASEBOARD:=volteer

# TODO: b/143375057 - Remove this code after power on.
#
# Temporary for board power on.  Provide a Volteer specific make option
# to enable the power signal GPIOs that are not stuffed by default.  This
# is a backup if board logic power sequencing needs to be adjusted.
#
# Set the following variable to 'y' to enable the Volteer optional power signals
VOLTEER_POWER_SEQUENCE=
ifneq (,$(VOLTEER_POWER_SEQUENCE))
CFLAGS_BASEBOARD+=-DVOLTEER_POWER_SEQUENCE
endif

# Force changes to VOLTEER_POWER_SEQUENCE variable to trigger a full build.
ENV_VARS := VOLTEER_POWER_SEQUENCE


board-y=board.o
board-$(VOLTEER_POWER_SEQUENCE)+=power_sequence.o
