/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_FLASH_CHIP_H
#define __CROS_EC_FLASH_CHIP_H

/*
 * This symbol is defined in linker script and used to provide the begin
 * address of the ram code section. With this address, we can enable a ILM
 * (4K bytes static code cache) for ram code section.
 */
extern const char __flash_dma_start;

/* This symbol is the begin address of the text section. */
extern const char __flash_text_start;

#endif /* __CROS_EC_FLASH_CHIP_H */
