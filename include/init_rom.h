/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Routines for accessing data objects store in the .init_rom region.
 * Enabled with the CONFIG_CHIP_INIT_ROM_REGION config option. Data
 * objects are placed into the .init_rom region using the __init_rom attribute.
 */

#ifndef __CROS_EC_INIT_ROM_H
#define __CROS_EC_INIT_ROM_H

#include "stdbool.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Get the memory mapped address of an .init_rom data object.
 *
 * @param offset	Address of the data object assigned by the linker.
 *			This is effectively a flash offset when
 *			CONFIG_CHIP_INIT_ROM_REGION is enabled, otherwise
 *			it is a regular address.
 * @param size	        Size of the data object.
 *
 * @return Pointer to data object in memory. Return NULL if the object
 * is not memory mapped.
 */
#ifdef CONFIG_CHIP_INIT_ROM_REGION
const void *init_rom_map(const void *addr, int size);
#else
#define init_rom_map(addr, size) (addr)
#endif

/**
 * Unmaps an .init_rom data object. Must be called when init_rom_map() is
 * successful.
 *
 * @param offset	Address of the data object assigned by the linker.
 * @param size	        Size of the data object.
 */
#ifdef CONFIG_CHIP_INIT_ROM_REGION
void init_rom_unmap(const void *addr, int size);
#else
#define init_rom_unmap(addr, size) (void)0
#endif

/**
 * Copy an .init_rom data object into a RAM location. This routine must be used
 * if init_rom_get_addr() returns NULL. This routine automatically handles
 * locking of the flash.
 *
 * @param offset	Flash offset of the data object.
 * @param size	        Size of the data object.
 * @param data		Destination buffer for data.
 *
 * @return 0 on success.
 */
#ifdef CONFIG_CHIP_INIT_ROM_REGION
int init_rom_copy(int offset, int size, char *data);
#else
#define init_rom_copy(offset, size, data) 0
#endif

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_INIT_ROM_H */
