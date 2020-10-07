/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CONFIG_CHIP_IT8XXX2_H
#define __CROS_EC_CONFIG_CHIP_IT8XXX2_H

/* CPU core BFD configuration */
#include "core/riscv-rv32i/config_core.h"

/* RISCV core */
#define CHIP_CORE_RISCV
#define CHIP_ILM_DLM_ORDER
/* The base address of EC interrupt controller registers. */
#define CHIP_EC_INTC_BASE           0x00F03F00
#define CONFIG_IT83XX_FLASH_CLOCK_48MHZ
/*
 * ILM/DLM size register.
 * bit[3-0] ILM size:
 *     7: 512K byte (default setting), 8: 1M byte
 */
#define IT83XX_GCTRL_EIDSR 0xf02031

/****************************************************************************/
/* Memory mapping */

#define CHIP_ILM_BASE               0x80000000
#define CHIP_EXTRA_STACK_SPACE      128
/* We reserve 12KB space for ramcode, h2ram, and immu sections. */
#define CHIP_RAM_SPACE_RESERVED     0x3000
#define CONFIG_PROGRAM_MEMORY_BASE  (CHIP_ILM_BASE)

/****************************************************************************/
/* Chip IT83202 is used with IT8XXX2 TCPM driver */
#define CONFIG_USB_PD_TCPM_DRIVER_IT8XXX2

#if defined(CHIP_VARIANT_IT83202BX)
/* TODO(b/133460224): enable properly chip config option. */
#define CONFIG_FLASH_SIZE           0x00080000
#define CONFIG_RAM_BASE             0x80080000
#define CONFIG_RAM_SIZE             0x00010000

/* Embedded flash is KGD */
#define IT83XX_CHIP_FLASH_IS_KGD
/* Don't let internal flash go into deep power down mode. */
#define IT83XX_CHIP_FLASH_NO_DEEP_POWER_DOWN
/* chip id is 3 bytes */
#define IT83XX_CHIP_ID_3BYTES
/*
 * The bit19 of ram code base address is controlled by bit7 of register SCARxH
 * instead of bit3.
 */
#define IT83XX_DAM_ADDR_BIT19_AT_REG_SCARXH_BIT7
/*
 * Disable eSPI pad, then PLL change
 * (include EC clock frequency) is succeed even CS# is low.
 */
#define IT83XX_ESPI_INHIBIT_CS_BY_PAD_DISABLED
/* The slave frequency is adjustable (bit[2-0] at register IT83XX_ESPI_GCAC1) */
#define IT83XX_ESPI_SLAVE_MAX_FREQ_CONFIGURABLE
/* Watchdog reset supports hardware reset. */
#define IT83XX_ETWD_HW_RESET_SUPPORT
/*
 * More GPIOs can be set as 1.8v input.
 * Please refer to gpio_1p8v_sel[] for 1.8v GPIOs.
 */
#define IT83XX_GPIO_1P8V_PIN_EXTENDED
/* All GPIOs support interrupt on rising, falling, and either edge. */
#define IT83XX_GPIO_INT_FLEXIBLE
/* Remap host I/O cycles to base address of H2RAM section. */
#define IT83XX_H2RAM_REMAPPING
/* Enable FRS detection interrupt. */
#define IT83XX_INTC_FAST_SWAP_SUPPORT
/* Enable detect type-c plug in and out interrupt. */
#define IT83XX_INTC_PLUG_IN_OUT_SUPPORT
/* Chip IT83202BX actually has TCPC physical port count. */
#define IT83XX_USBPD_PHY_PORT_COUNT    3
#elif defined(CHIP_VARIANT_IT81302AX_1024) \
|| defined(CHIP_VARIANT_IT81202AX_1024)
#define CONFIG_FLASH_SIZE           0x00100000
#define CONFIG_RAM_BASE             0x80100000
#define CONFIG_RAM_SIZE             0x0000f000

/* Embedded flash is KGD */
#define IT83XX_CHIP_FLASH_IS_KGD
/* Set ILM (instruction local memory) size up to 1M bytes */
#define IT83XX_CHIP_FLASH_SIZE_1MB
/* chip id is 3 bytes */
#define IT83XX_CHIP_ID_3BYTES
/*
 * The bit19 of ram code base address is controlled by bit7 of register SCARxH
 * instead of bit3.
 */
#define IT83XX_DAM_ADDR_BIT19_AT_REG_SCARXH_BIT7
/*
 * Disable eSPI pad, then PLL change
 * (include EC clock frequency) is succeed even CS# is low.
 */
#define IT83XX_ESPI_INHIBIT_CS_BY_PAD_DISABLED
/* The slave frequency is adjustable (bit[2-0] at register IT83XX_ESPI_GCAC1) */
#define IT83XX_ESPI_SLAVE_MAX_FREQ_CONFIGURABLE
/* Watchdog reset supports hardware reset. */
#define IT83XX_ETWD_HW_RESET_SUPPORT
/*
 * More GPIOs can be set as 1.8v input.
 * Please refer to gpio_1p8v_sel[] for 1.8v GPIOs.
 */
#define IT83XX_GPIO_1P8V_PIN_EXTENDED
#ifdef CHIP_VARIANT_IT81202AX_1024
/* Pins of group K and L are set as internal pull-down at initialization. */
#define IT83XX_GPIO_GROUP_K_L_DEFAULT_PULL_DOWN
#endif
/* GPIOH7 is set as output low at initialization. */
#define IT83XX_GPIO_H7_DEFAULT_OUTPUT_LOW
/* All GPIOs support interrupt on rising, falling, and either edge. */
#define IT83XX_GPIO_INT_FLEXIBLE
/* Remap host I/O cycles to base address of H2RAM section. */
#define IT83XX_H2RAM_REMAPPING
/* Enable FRS detection interrupt. */
#define IT83XX_INTC_FAST_SWAP_SUPPORT
/* Enable detect type-c plug in and out interrupt. */
#define IT83XX_INTC_PLUG_IN_OUT_SUPPORT
/* Wake up CPU from low power mode even if interrupts are disabled */
#define IT83XX_RISCV_WAKEUP_CPU_WITHOUT_INT_ENABLED
/* Auto reset rx fifo while CS# deasserted. */
#define IT83XX_SPI_AUTO_RESET_RX_FIFO
/* CPU accesses FIFO to reach rx valid data length. */
#define IT83XX_SPI_RX_VALID_INT
/* Individual setting CC1 and CC2 resistance. */
#define IT83XX_USBPD_CC1_CC2_RESISTANCE_SEPARATE
/* Chip actually has TCPC physical port count. */
#define IT83XX_USBPD_PHY_PORT_COUNT    2
#else
#error "Unsupported chip variant!"
#endif

#define CHIP_H2RAM_BASE   (CONFIG_RAM_BASE + 0x1000) /* base+1000h~base+1FFF */
#define CHIP_RAMCODE_BASE (CONFIG_RAM_BASE + 0x2000) /* base+2000h~base+2FFF */

#endif  /* __CROS_EC_CONFIG_CHIP_IT8XXX2_H */
