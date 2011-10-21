/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * flash.h - flash interface
 */

#ifndef __CHIP_INTERFACE_FLASH_H
#define __CHIP_INTERFACE_FLASH_H

/* returns the flash size and erase clock structure. */
EcError EcFlashQuery(*size, *erase_block) {

EcError EcFlashRead(addr_t offset, length, uint8_t *data);

EcError EcFlashWrite(addr_t offset, length, uint8_t *data);

EcError EcFlashErase(addr_t offset, length);

EcError EcFlashSetWriteProtectRange(addr_t offset, length);

/* TODO: change to Randall's new model */
/* Set lock bit (the SRP bit in SPI) */
EcError EcFlashEnableWriteProtect(void);

/* TODO: change to Randall's new model */
/* Always return FAILED because FMPPEn cannot be reset to 1
 * until a power-on reset or after committed (see p. 577).
 */
int EcFlashDisableWriteProtect(void);

#endif  /* __CHIP_INTERFACE_FLASH_H */
