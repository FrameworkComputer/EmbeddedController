/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_PMU_H
#define __CROS_EC_PMU_H

#include "common.h"
#include "registers.h"

enum {
	/* RO */
	PERIPH_CAMO             = 0x0,
	PERIPH_CAMO0            = 0x0,

	PERIPH_CRYPTO           = 0x1,
	PERIPH_CRYPTO0          = 0x1,

	PERIPH_DMA              = 0x2,
	PERIPH_DMA0             = 0x2,

	PERIPH_FLASH            = 0x3,
	PERIPH_FLASH0           = 0x3,

	PERIPH_FUSE             = 0x4,
	PERIPH_FUSE0            = 0x4,

	/* RO */
	PERIPH_GLOBALSEC        = 0x5,
	PERIPH_GLOBALSEC_TIMER  = 0x6,
	PERIPH_GLOBALSEC_HS     = 0x7,

	PERIPH_GPIO             = 0x8,
	PERIPH_GPIO0            = 0x8,
	PERIPH_GPIO1            = 0x9,

	PERIPH_I2C              = 0xa,
	PERIPH_I2C0             = 0xa,
	PERIPH_I2C1             = 0xb,

	PERIPH_I2CS             = 0xc,
	PERIPH_I2CS0            = 0xc,

	PERIPH_KEYMGR           = 0xd,
	PERIPH_KEYMGR0          = 0xd,

	/* RO */
	PERIPH_APB0             = 0xe,
	PERIPH_APB1             = 0xf,
	PERIPH_APB2             = 0x10,
	PERIPH_APB2_TIMER       = 0x11,
	PERIPH_APB3             = 0x12,
	PERIPH_APB3_HS          = 0x13,

	PERIPH_PINMUX           = 0x14,

	PERIPH_PMU              = 0x15,

	PERIPH_RBOX             = 0x16,
	PERIPH_RBOX0            = 0x16,

	PERIPH_RDD              = 0x17,
	PERIPH_RDD0             = 0x17,

	PERIPH_RTC              = 0x18,
	PERIPH_RTC0             = 0x18,
	PERIPH_RTC_TIMER        = 0x19,
	PERIPH_RTC0_TIMER       = 0x19,

	PERIPH_SPI              = 0x1a,
	PERIPH_SPI0             = 0x1a,
	PERIPH_SPI1             = 0x1b,

	PERIPH_SPS              = 0x1c,
	PERIPH_SPS0             = 0x1c,
	PERIPH_SPS0_TIMER       = 0x1d,

	PERIPH_SWDP             = 0x1e,
	PERIPH_SWDP0            = 0x1e,

	/* RO */
	PERIPH_TEMP             = 0x1f,
	PERIPH_TEMP0            = 0x1f,

	PERIPH_TIMEHS           = 0x20,
	PERIPH_TIMEHS0          = 0x20,
	PERIPH_TIMEHS1          = 0x21,

	PERIPH_TIMELS           = 0x22,
	PERIPH_TIMELS0          = 0x22,

	PERIPH_TIMEUS           = 0x23,
	PERIPH_TIMEUS0          = 0x23,

	PERIPH_TRNG             = 0x24,
	PERIPH_TRNG0            = 0x24,

	PERIPH_UART             = 0x25,
	PERIPH_UART0            = 0x25,
	PERIPH_UART1            = 0x26,
	PERIPH_UART2            = 0x27,

	PERIPH_USB              = 0x28,
	PERIPH_USB0             = 0x28,
	PERIPH_USB0_USB_PHY     = 0x29,

	/* RO */
	PERIPH_VOLT             = 0x2a,
	PERIPH_VOLT0            = 0x2a,

	/* RO */
	PERIPH_WATCHDOG         = 0x2b,
	PERIPH_WATCHDOG0        = 0x2b,

	PERIPH_XO               = 0x2c,
	PERIPH_XO0              = 0x2c,
	PERIPH_XO_TIMER         = 0x2d,
	PERIPH_XO0_TIMER        = 0x2d,

	/* RO */
	PERIPH_MASTER_MATRIX    = 0x2e,
	PERIPH_MATRIX           = 0x2f,
};

typedef void (*pmu_clock_func)(uint32_t periph);
extern void pmu_clock_en(uint32_t periph);
extern void pmu_clock_dis(uint32_t periph);
extern void pmu_peripheral_rst(uint32_t periph);
extern uint32_t pmu_calibrate_rc_trim(void);
extern uint32_t pmu_clock_switch_rc_notrim(void);
extern uint32_t pmu_clock_switch_rc_trim(uint32_t skip_calibration);
extern uint32_t pmu_clock_switch_xo(void);
extern void pmu_sleep(void);
extern void pmu_hibernate(void);
extern void pmu_hibernate_exit(void);
extern void pmu_powerdown(void);
extern void pmu_powerdown_exit(void);

/*
 * enable clock doubler for USB purposes
 */
void pmu_enable_clock_doubler(void);
#endif /* __CROS_EC_PMU_H */
