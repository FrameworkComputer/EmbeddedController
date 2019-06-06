/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* MAX32660 Clocks and Power Management Module for Chrome EC */

#include "clock.h"
#include "common.h"
#include "console.h"
#include "cpu.h"
#include "hooks.h"
#include "hwtimer.h"
#include "registers.h"
#include "system.h"
#include "timer.h"
#include "util.h"
#include "watchdog.h"
#include "tmr_regs.h"
#include "gcr_regs.h"
#include "pwrseq_regs.h"

#define MAX32660_SYSTEMCLOCK SYS_CLOCK_HIRC

/** Clock source */
typedef enum {
	SYS_CLOCK_NANORING = MXC_V_GCR_CLKCN_CLKSEL_NANORING, /**< 8KHz nanoring
								 on MAX32660 */
	SYS_CLOCK_HFXIN =
		MXC_V_GCR_CLKCN_CLKSEL_HFXIN, /**< 32KHz on MAX32660 */
	SYS_CLOCK_HFXIN_DIGITAL = 0x9,	/**< External Clock Input*/
	SYS_CLOCK_HIRC = MXC_V_GCR_CLKCN_CLKSEL_HIRC, /**< High Frequency
							 Internal Oscillator */
} sys_system_clock_t;

/***** Functions ******/
static void clock_wait_ready(uint32_t ready)
{
	// Start timeout, wait for ready
	do {
		if (MXC_GCR->clkcn & ready) {
			return;
		}
	} while (1);
}

extern void (*const __isr_vector[])(void);
uint32_t SystemCoreClock = HIRC96_FREQ;

static void clock_update(void)
{
	uint32_t base_freq, divide, ovr;

	// Get the clock source and frequency
	ovr = (MXC_PWRSEQ->lp_ctrl & MXC_F_PWRSEQ_LP_CTRL_OVR);
	if (ovr == MXC_S_PWRSEQ_LP_CTRL_OVR_0_9V) {
		base_freq = HIRC96_FREQ / 4;
	} else {
		if (ovr == MXC_S_PWRSEQ_LP_CTRL_OVR_1_0V) {
			base_freq = HIRC96_FREQ / 2;
		} else {
			base_freq = HIRC96_FREQ;
		}
	}

	// Get the clock divider
	divide = (MXC_GCR->clkcn & MXC_F_GCR_CLKCN_PSC) >>
		 MXC_F_GCR_CLKCN_PSC_POS;

	SystemCoreClock = base_freq >> divide;
}

void clock_init(void)
{
	/* Switch system clock to HIRC */
	uint32_t ovr, divide;

	// Set FWS higher than what the minimum for the fastest clock is
	MXC_GCR->memckcn = (MXC_GCR->memckcn & ~(MXC_F_GCR_MEMCKCN_FWS)) |
			   (0x5UL << MXC_F_GCR_MEMCKCN_FWS_POS);

	// Enable 96MHz Clock
	MXC_GCR->clkcn |= MXC_F_GCR_CLKCN_HIRC_EN;

	// Wait for the 96MHz clock
	clock_wait_ready(MXC_F_GCR_CLKCN_HIRC_RDY);

	// Set 96MHz clock as System Clock
	MXC_SETFIELD(MXC_GCR->clkcn, MXC_F_GCR_CLKCN_CLKSEL,
		     MXC_S_GCR_CLKCN_CLKSEL_HIRC);

	// Wait for system clock to be ready
	clock_wait_ready(MXC_F_GCR_CLKCN_CKRDY);

	// Update the system core clock
	clock_update();

	// Get the clock divider
	divide = (MXC_GCR->clkcn & MXC_F_GCR_CLKCN_PSC) >>
		 MXC_F_GCR_CLKCN_PSC_POS;

	// get ovr setting
	ovr = (MXC_PWRSEQ->lp_ctrl & MXC_F_PWRSEQ_LP_CTRL_OVR);

	// Set flash wait settings
	if (ovr == MXC_S_PWRSEQ_LP_CTRL_OVR_0_9V) {
		if (divide == 0) {
			MXC_GCR->memckcn =
				(MXC_GCR->memckcn & ~(MXC_F_GCR_MEMCKCN_FWS)) |
				(0x2UL << MXC_F_GCR_MEMCKCN_FWS_POS);
		} else {
			MXC_GCR->memckcn =
				(MXC_GCR->memckcn & ~(MXC_F_GCR_MEMCKCN_FWS)) |
				(0x1UL << MXC_F_GCR_MEMCKCN_FWS_POS);
		}
	} else if (ovr == MXC_S_PWRSEQ_LP_CTRL_OVR_1_0V) {
		if (divide == 0) {
			MXC_GCR->memckcn =
				(MXC_GCR->memckcn & ~(MXC_F_GCR_MEMCKCN_FWS)) |
				(0x2UL << MXC_F_GCR_MEMCKCN_FWS_POS);
		} else {
			MXC_GCR->memckcn =
				(MXC_GCR->memckcn & ~(MXC_F_GCR_MEMCKCN_FWS)) |
				(0x1UL << MXC_F_GCR_MEMCKCN_FWS_POS);
		}
	} else {
		if (divide == 0) {
			MXC_GCR->memckcn =
				(MXC_GCR->memckcn & ~(MXC_F_GCR_MEMCKCN_FWS)) |
				(0x4UL << MXC_F_GCR_MEMCKCN_FWS_POS);
		} else if (divide == 1) {
			MXC_GCR->memckcn =
				(MXC_GCR->memckcn & ~(MXC_F_GCR_MEMCKCN_FWS)) |
				(0x2UL << MXC_F_GCR_MEMCKCN_FWS_POS);
		} else {
			MXC_GCR->memckcn =
				(MXC_GCR->memckcn & ~(MXC_F_GCR_MEMCKCN_FWS)) |
				(0x1UL << MXC_F_GCR_MEMCKCN_FWS_POS);
		}
	}
}
