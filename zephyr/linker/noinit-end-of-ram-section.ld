/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * noinit_end_of_ram is a section of uninitialized ram placed
 * at the very end of used ram, where it is least likely
 * to overlap with ram RO ram. The section is useful for preserving
 * data across reboots.
 */

SECTION_PROLOGUE(.noinit_end_of_ram,(NOLOAD),)
{
        . = ALIGN(4);
        __noinit_end_of_ram_start = .;
        . += CONFIG_PLATFORM_EC_NOINIT_END_OF_RAM_SECTION_OFFSET;
        . = ALIGN(4);
        *(.noinit_end_of_ram.*)
        __noinit_end_of_ram_end = .;
        . = ALIGN(4);
} GROUP_LINK_IN(RAMABLE_REGION)
