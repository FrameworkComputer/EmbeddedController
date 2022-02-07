/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * DFU Boot Manager shared utilities for STM32
 *
 * These of utilities required by the Boot Manager Main and RW Application.
 * These allow the systems to start the sequence to enter DFU, clear the
 * state, and access the backup registers.
 */

#ifndef __DFU_BOOTMANAGER_SHARED_H
#define __DFU_BOOTMANAGER_SHARED_H

#include "common.h"

/* Registers to validate the backup memory region. */
#define DFU_BOOTMANAGER_VALUE_MASK      0x00FF
#define DFU_BOOTMANAGER_VALID_MASK      0xFF00
#define DFU_BOOTMANAGER_VALID_CHECK     0xAA00

#define DFU_BOOTMANAGER_VALUE_CLEAR     0
#define DFU_BOOTMANAGER_VALUE_DFU       UINT8_MAX

/*
 * Reset and enter the DFU mode.
 *
 * The system will set flags to the backup memory to enter DFU and issue
 * a reset. The Boot Manager will send the system to DFU mode.
 *
 * @return           EC_SUCCESS, or non-zero if error.
 */
int dfu_bootmanager_enter_dfu(void);

/*
 * Clear the DFU Boot Manager backup memory state.
 *
 * If the CONFIG_DFU_BOOTMANAGER_MAX_REBOOT_COUNT setting is enabled, this
 * allows the RW application to clear the counter to indicate the application
 * is working as expected.
 */
void dfu_bootmanager_clear(void);

/*
 * Write the value to the backup registers and sets the bitmasks
 * indicating the field is valid.
 *
 * @param value  New value to store.
 */
void dfu_bootmanager_backup_write(uint8_t value);

/*
 * Reads the backup registers and performs validation. The value stored
 * within the VALUE_MASK is returned and the status code indicates
 * if the valid check passed.
 *
 * @param value[out] Value stored within the DFU_BOOTMANAGER_VALUE_MASK
 * @return           EC_SUCCESS, or non-zero if validation failed.
 */
int dfu_bootmanager_backup_read(uint8_t *value);

#endif /* __DFU_BOOTMANAGER_SHARED_H */
