/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_RAM_LOCK_H
#define __CROS_EC_RAM_LOCK_H

/**
 * Configure lock region for NPCX
 */
int ram_lock_config_lock_region(uint8_t region, uint32_t addr, uint32_t size);

#endif /* __CROS_EC_RAM_LOCK_H */
