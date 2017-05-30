/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EC_INCLUDE_NVCOUNTER_H
#define __EC_INCLUDE_NVCOUNTER_H

/*
 * CONFIG_FLASH_NVCOUNTER provides a robust, non-volatile incrementing counter.
 *
 * It is currently uses 2 physical pages of flash for its underlying storage
 * which are configured by CONFIG_FLASH_NVCTR_BASE_A and
 * CONFIG_FLASH_NVCTR_BASE_B in board.h
 */

/* return the value of the counter after incrementing it */
uint32_t nvcounter_incr(void);

#endif	/* __EC_INCLUDE_NVCOUNTER_H */
