# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources("src/mtlrvp.c")
zephyr_library_sources("src/board_power.c")
zephyr_library_sources_ifdef(CONFIG_BOARD_MTLRVP_PD "src/pd.c")

if((DEFINED CONFIG_BOARD_MTLRVP_M1723) OR (DEFINED CONFIG_BOARD_MTLRVP_MCHP) OR (DEFINED CONFIG_BOARD_MTLRVP_NPCX))
  zephyr_library_sources("src/tcpc.c")
endif()
