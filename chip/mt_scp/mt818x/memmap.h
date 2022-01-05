/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * SCP memory map
 */

#ifndef __CROS_EC_MEMMAP_H
#define __CROS_EC_MEMMAP_H

void scp_memmap_init(void);

/**
 * Translate AP addr to SCP addr.
 *
 * @param ap_addr		AP address to translate
 * @param scp_addr		Tranlated AP address
 * @return			EC_SUCCESS or EC_ERROR_INVAL
 */
int memmap_ap_to_scp(uintptr_t ap_addr, uintptr_t *scp_addr);

/**
 * Translate SCP addr to AP addr.
 *
 * @param scp_addr		SCP address to tranlate
 * @param ap_addr		Translated SCP address
 * @return			EC_SUCCESS or EC_ERROR_INVAL
 */
int memmap_scp_to_ap(uintptr_t scp_addr, uintptr_t *ap_addr);

/**
 * Translate AP addr to SCP cache addr.
 *
 * @param ap_addr		AP address to translate
 * @param scp_addr		Tranlated AP cache address
 * @return			EC_SUCCESS or EC_ERROR_INVAL
 */
int memmap_ap_to_scp_cache(uintptr_t ap_addr, uintptr_t *scp_addr);

/**
 * Translate SCP addr to AP addr.
 *
 * @param scp_addr		SCP cache address to tranlate
 * @param ap_addr		Translated SCP cache address
 * @return			EC_SUCCESS or EC_ERROR_INVAL
 */
int memmap_scp_cache_to_ap(uintptr_t scp_addr, uintptr_t *ap_addr);

#endif /* #ifndef __CROS_EC_MEMMAP_H */
