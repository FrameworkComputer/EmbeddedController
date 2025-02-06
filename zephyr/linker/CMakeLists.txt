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

if(DEFINED CONFIG_PLATFORM_EC_NOINIT_END_OF_RAM_SECTION)
  if(DEFINED CONFIG_SOC_IT8XXX2)
    # SOC_IT8XXX2 layout is special, snippets-sections.ld is included last.
    zephyr_linker_sources(SECTIONS SORT_KEY zzz noinit-end-of-ram-section.ld)
  else()
    zephyr_linker_sources(NOINIT SORT_KEY zzz noinit-end-of-ram.ld)
  endif()
endif()
