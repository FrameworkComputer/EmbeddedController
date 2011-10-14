/* flash.h - flash interface
 *
 * (Chromium license) */

#ifndef __CHIP_INTERFACE_FLASH_H
#define __CHIP_INTERFACE_FLASH_H

/* returns the flash size and erase clock structure. */
EcError CrFlashQuery(*size, *erase_block) {

EcError CrFlashRead(addr_t offset, length, uint8_t *data);

EcError CrFlashWrite(addr_t offset, length, uint8_t *data);

EcError CrFlashErase(addr_t offset, length);

EcError CrFlashSetWriteProtectRange(addr_t offset, length);

/* TODO: change to Randall's new model */
/* Set lock bit (the SRP bit in SPI) */
EcError CrFlashEnableWriteProtect(void);

/* TODO: change to Randall's new model */
/* Always return FAILED because FMPPEn cannot be reset to 1
 * until a power-on reset or after committed (see p. 577).
 */
int CrFlashDisableWriteProtect(void);

#endif  /* __CHIP_INTERFACE_FLASH_H */
