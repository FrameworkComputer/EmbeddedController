/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EC_CHIP_G_LOADER_ROM_FLASH_H
#define __EC_CHIP_G_LOADER_ROM_FLASH_H

#include "registers.h"

#define FSH_OP_BULKERASE GC_CONST_FSH_PE_CONTROL_BULKERASE
#define FSH_OP_ENABLE    GC_CONST_FSH_PE_EN
#define FSH_OP_ERASE     GC_CONST_FSH_PE_CONTROL_ERASE
#define FSH_OP_PROGRAM   GC_CONST_FSH_PE_CONTROL_PROGRAM
#define FSH_OP_READ      GC_CONST_FSH_PE_CONTROL_READ

#if 0
#define num_flashes 2
/*
 * FIX ME: words_per_row = m.get_flash_param("FlashWordsPerRow")
 * FIX ME: rows_per_page = m.get_flash_param("FlashRowsPerPage")
 * FIX ME: words_per_page = int(words_per_row)*int(rows_per_page)
 */
#define words_per_page 512
/* This is BAD... This number is based on the TSMC spec Nmp=Tprog/Tsmp */
#define _max_prog_cycles 9
/* This is BAD... This number is based on the TSMC spec Nme=Terase/Tsme */
#define _max_erase_cycles 45
/* This is BAD... This number is based on the TSMC spec Nme=Terase/Tsme */
#define _max_bulkerase_cycles 45
#define _debug_flash 0

/* write words to flash */
int flash_write(uint32_t fidx, uint32_t offset,
		const uint32_t *data, uint32_t size);

/* erase one page */
int flash_erase(uint32_t fidx, uint32_t page);

/* erase entire bank */
int flash_wipe(uint32_t fidx);
#endif

#define E_FL_NOT_AWAKE 1
#define E_FL_TIMEOUT 2
#define E_FL_BAD_MAINB 3
#define E_FL_BAD_SIZE 4
#define E_FL_BAD_PTR 5
#define E_FL_BAD_BANK 6
#define E_FL_WRITE_FAIL 7
#define E_FL_ERASE_FAIL 8
#define E_FL_WIPE_FAIL 9
#define E_FL_ERROR 10

/* read single word from info block */
int flash_info_read(uint32_t offset, uint32_t *dst);


#endif  /* __EC_CHIP_G_LOADER_ROM_FLASH_H */
