/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_MEMMAP_H
#define __CROS_EC_MEMMAP_H

#include "stdint.h"

void memmap_init(void);

/**
 * Translate AP addr to SCP addr.
 *
 * @param ap_addr		AP address to translate
 * @param scp_addr		Translated AP address
 * @return			EC_SUCCESS or EC_ERROR_INVAL
 */
int memmap_ap_to_scp(uintptr_t ap_addr, uintptr_t *scp_addr);

/**
 * Translate SCP addr to AP addr.
 *
 * @param scp_addr		SCP address to translate
 * @param ap_addr		Translated SCP address
 * @return			EC_SUCCESS or EC_ERROR_INVAL
 */
int memmap_scp_to_ap(uintptr_t scp_addr, uintptr_t *ap_addr);

#endif /* #ifndef __CROS_EC_MEMMAP_H */
