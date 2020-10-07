/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CONFIG_CHIP_IT8320_H
#define __CROS_EC_CONFIG_CHIP_IT8320_H

/* CPU core BFD configuration */
#include "core/nds32/config_core.h"

/* N8 core */
#define CHIP_CORE_NDS32
/* The base address of EC interrupt controller registers. */
#define CHIP_EC_INTC_BASE           0x00F01100

/****************************************************************************/
/* Memory mapping */

#define CHIP_H2RAM_BASE             0x0008D000 /* 0x0008D000~0x0008DFFF */
#define CHIP_RAMCODE_BASE           0x0008E000 /* 0x0008E000~0x0008EFFF */
#define CHIP_EXTRA_STACK_SPACE      0

#define CONFIG_RAM_BASE             0x00080000
#define CONFIG_RAM_SIZE             0x0000C000

#define CONFIG_PROGRAM_MEMORY_BASE  0x00000000

/****************************************************************************/
/* Chip IT8320 is used with IT83XX TCPM driver */
#define CONFIG_USB_PD_TCPM_DRIVER_IT83XX

#if defined(CHIP_VARIANT_IT8320BX)
/* This is the physical size of the flash on the chip. We'll reserve one bank
 * in order to emulate per-bank write-protection UNTIL REBOOT. The hardware
 * doesn't support a write-protect pin, and if we make the write-protection
 * permanent, it can't be undone easily enough to support RMA.
 */
#define CONFIG_FLASH_SIZE  0x00040000
/* For IT8320BX, we have to reload cc parameters after ec softreset. */
#define IT83XX_USBPD_CC_PARAMETER_RELOAD
/*
 * The voltage detector of CC1 and CC2 is enabled/disabled by different bit
 * of the control register (bit1 and bit5 at register IT83XX_USBPD_CCCSR).
 */
#define IT83XX_USBPD_CC_VOLTAGE_DETECTOR_INDEPENDENT
/* Chip IT8320BX actually has TCPC physical port count */
#define IT83XX_USBPD_PHY_PORT_COUNT    2
/* For IT8320BX, we have to write 0xff to clear pending bit.*/
#define IT83XX_ESPI_VWCTRL1_WRITE_FF_CLEAR
/* For IT8320BX, we have to read observation register of external timer two
 * times to get correct time.
 */
#define IT83XX_EXT_OBSERVATION_REG_READ_TWO_TIMES
#elif defined(CHIP_VARIANT_IT8320DX)
#define CONFIG_FLASH_SIZE  0x00080000
#define CONFIG_IT83XX_FLASH_CLOCK_48MHZ
/*
 * Disable eSPI pad, then PLL change
 * (include EC clock frequency) is succeed even CS# is low.
 */
#define IT83XX_ESPI_INHIBIT_CS_BY_PAD_DISABLED
/* The slave frequency is adjustable (bit[2-0] at register IT83XX_ESPI_GCAC1) */
#define IT83XX_ESPI_SLAVE_MAX_FREQ_CONFIGURABLE
/*
 * TODO(b/111480168): eSPI HW reset can't be used because the DMA address
 * gets set incorrectly resulting in a memory access exception.
 */
#define IT83XX_ESPI_RESET_MODULE_BY_FW
/* Watchdog reset supports hardware reset. */
/* TODO(b/111264984): watchdog hardware reset function failed. */
#undef IT83XX_ETWD_HW_RESET_SUPPORT
/*
 * (b/112452221):
 * Floating-point multiplication single-precision is failed on DX version,
 * so we use the formula "A/(1/B)" to replace a multiplication operation
 * (A*B = A/(1/B)).
 */
#define IT83XX_FPU_MUL_BY_DIV
/*
 * More GPIOs can be set as 1.8v input.
 * Please refer to gpio_1p8v_sel[] for 1.8v GPIOs.
 */
#define IT83XX_GPIO_1P8V_PIN_EXTENDED
/* All GPIOs support interrupt on rising, falling, and either edge. */
#define IT83XX_GPIO_INT_FLEXIBLE
/* Enable FRS detection interrupt. */
#define IT83XX_INTC_FAST_SWAP_SUPPORT
/* Enable interrupts of group 21 and 22. */
#define IT83XX_INTC_GROUP_21_22_SUPPORT
/* Enable detect type-c plug in and out interrupt. */
#define IT83XX_INTC_PLUG_IN_OUT_SUPPORT
/* Chip Dx transmit status bit of PD register is different from Bx. */
#define IT83XX_PD_TX_ERROR_STATUS_BIT5
/* Chip IT8320DX actually has TCPC physical port count */
#define IT83XX_USBPD_PHY_PORT_COUNT    2
#else
#error "Unsupported chip variant!"
#endif

#endif  /* __CROS_EC_CONFIG_CHIP_IT8320_H */
