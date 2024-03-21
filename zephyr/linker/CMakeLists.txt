# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Add the fixed sections to the output image.
zephyr_linker_sources(ROM_START SORT_KEY 1 fixed-sections.ld)

# Support protection of part of the internal RAM
zephyr_linker_sources(RWDATA SORT_KEY 1 iram_text.ld)

# Compute the image size
zephyr_linker_sources(RAM_SECTIONS image_size.ld)

# Little FW with specific purposes used by NPCX EC
zephyr_linker_sources_ifdef(CONFIG_SOC_FAMILY_NPCX ROM_START SORT_KEY 1
  npcx-lfw.ld)

# Little FW with specific purposes used by MCHP XEC EC family
zephyr_linker_sources_ifdef(CONFIG_SOC_FAMILY_MICROCHIP_MEC ROM_START SORT_KEY 1
  mchp-xec-lfw.ld)

zephyr_linker_sources(DATA_SECTIONS iterables-ram.ld)
zephyr_linker_sources(SECTIONS iterables-rom.ld)
