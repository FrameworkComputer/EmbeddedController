/*
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* NPCX-specific SIB module for Chrome EC */

/* Super-IO index and register definitions */
#define INDEX_SID       0x20
#define INDEX_CHPREV    0x24
#define INDEX_SRID      0x27

#define SIO_OFFSET      0x4E

/* Super-IO register write function */
void sib_write_reg(uint8_t io_offset, uint8_t index_value,
		uint8_t io_data);
/* Super-IO register read function */
uint8_t sib_read_reg(uint8_t io_offset, uint8_t index_value);
/* Emulate host to read Keyboard I/O */
uint8_t sib_read_kbc_reg(uint8_t io_offset);

