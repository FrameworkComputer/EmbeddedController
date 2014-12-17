/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef INC_PMU_H_
#define INC_PMU_H_

#include "common.h"
#include "registers.h"

enum {
	PERIPH_AES              = 0x0,
	PERIPH_AES0             = 0x0,
	PERIPH_AES1             = 0x1,

	PERIPH_CAMO             = 0x2,
	PERIPH_CAMO0            = 0x2,

	PERIPH_FLASH            = 0x3,
	PERIPH_FLASH0           = 0x3,

	GLOBALSEC               = 0x4,
	GLOBALSEC0              = 0x4,

	PERIPH_GPIO             = 0x5,
	PERIPH_GPIO0            = 0x5,
	PERIPH_GPIO1            = 0x6,

	PERIPH_I2C              = 0x7,
	PERIPH_I2C0             = 0x7,
	PERIPH_I2C1             = 0x8,

	PERIPH_I2CS             = 0x9,
	PERIPH_I2CS0            = 0x9,

	PERIPH_MAU              = 0xa,
	PERIPH_PAU              = 0xb,
	PERIPH_PINMUX           = 0xc,
	PERIPH_PMU              = 0xd,

	PERIPH_RBOX             = 0xe,
	PERIPH_RBOX0            = 0xe,

	PERIPH_RTC              = 0xf,
	PERIPH_RTC0             = 0xf,

	PERIPH_SHA              = 0x10,
	PERIPH_SHA0             = 0x10,

	PERIPH_SPI              = 0x11,
	PERIPH_SPI0             = 0x11,

	PERIPH_SPS              = 0x12,
	PERIPH_SPS0             = 0x12,

	PERIPH_SWDP             = 0x13,
	PERIPH_SWDP0            = 0x13,

	PERIPH_TEMP             = 0x14,
	PERIPH_TEMP0            = 0x14,

	PERIPH_TIMEHS           = 0x15,
	PERIPH_TIMEHS0          = 0x15,
	PERIPH_TIMEHS1          = 0x16,

	PERIPH_TIMELS           = 0x17,
	PERIPH_TIMELS0          = 0x17,

	PERIPH_TRNG             = 0x18,
	PERIPH_TRNG0            = 0x18,

	PERIPH_UART             = 0x19,
	PERIPH_UART0            = 0x19,
	PERIPH_UART1            = 0x1a,
	PERIPH_UART2            = 0x1b,

	PERIPH_USB              = 0x1c,
	PERIPH_USB0             = 0x1c,
	PERIPH_USB0_USB_PHY     = 0x1d,

	PERIPH_WATCHDOG         = 0x1e,
	PERIPH_WATCHDOG0        = 0x1e,

	PERIPH_XO               = 0x1f,
	PERIPH_XO0              = 0x1f,

	PERIPH_PERI             = 0x20,
	PERIPH_PERI0            = 0x20,
	PERIPH_PERI1            = 0x21,

	PERIPH_PERI_MATRIX      = 0x22,
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
#endif /* INC_PMU_H_ */
