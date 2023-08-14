/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cec.h"
#include "clock_chip.h"
#include "console.h"
#include "driver/cec/bitbang.h"
#include "fan_chip.h"
#include "registers.h"
#include "task.h"
#include "util.h"

#if !(DEBUG_CEC)
#define CPRINTF(...)
#define CPRINTS(...)
#else
#define CPRINTF(format, args...) cprintf(CC_CEC, format, ##args)
#define CPRINTS(format, args...) cprints(CC_CEC, format, ##args)
#endif

/*
 * Only one instance of the bitbang driver is supported on Nuvoton for now.
 * TODO(b/281643331): Detect the port from the events in cec_isr() instead.
 */
static int cec_port;

/*
 * Time between interrupt triggered and the next timer was
 * set when measuring pulse width
 */
static int cap_delay;

/* Value charged into the capture timer on last capture start */
static int cap_charge;

/* APB1 frequency. Store divided by 10k to avoid some runtime divisions */
uint32_t apb1_freq_div_10k;

/* Software generated interrupt triggers the send logic */
static uint8_t sw_interrupt;

void cec_tmr_cap_start(int port, enum cec_cap_edge edge, int timeout)
{
	int mdl = NPCX_MFT_MODULE_1;

	if (edge == CEC_CAP_EDGE_NONE) {
		/*
		 * If edge is NONE, disable capture interrupts and wait for a
		 * timeout only.
		 */
		CLEAR_BIT(NPCX_TIEN(mdl), NPCX_TIEN_TAIEN);
	} else {
		/* Select edge to trigger capture on */
		UPDATE_BIT(NPCX_TMCTRL(mdl), NPCX_TMCTRL_TAEDG,
			   edge == CEC_CAP_EDGE_RISING);
		SET_BIT(NPCX_TIEN(mdl), NPCX_TIEN_TAIEN);
	}

	/*
	 * Set capture timeout. If we don't have a timeout, we
	 * turn the timeout interrupt off and only care about
	 * the edge change.
	 */
	if (timeout > 0) {
		/*
		 * Store the time it takes from the interrupts starts to when we
		 * actually get here. This part of the pulse-width needs to be
		 * taken into account
		 */
		cap_delay = (0xffff - NPCX_TCNT1(mdl));
		cap_charge = timeout - cap_delay;
		NPCX_TCNT1(mdl) = cap_charge;
		SET_BIT(NPCX_TIEN(mdl), NPCX_TIEN_TCIEN);
	} else {
		CLEAR_BIT(NPCX_TIEN(mdl), NPCX_TIEN_TCIEN);
		NPCX_TCNT1(mdl) = 0;
	}

	/* Clear out old events */
	SET_BIT(NPCX_TECLR(mdl), NPCX_TECLR_TACLR);
	SET_BIT(NPCX_TECLR(mdl), NPCX_TECLR_TCCLR);
	NPCX_TCRA(mdl) = 0;
	/* Start the capture timer */
	SET_FIELD(NPCX_TCKC(mdl), NPCX_TCKC_C1CSEL_FIELD, 1);
}

void cec_tmr_cap_stop(int port)
{
	int mdl = NPCX_MFT_MODULE_1;

	CLEAR_BIT(NPCX_TIEN(mdl), NPCX_TIEN_TCIEN);
	SET_FIELD(NPCX_TCKC(mdl), NPCX_TCKC_C1CSEL_FIELD, 0);
}

int cec_tmr_cap_get(int port)
{
	int mdl = NPCX_MFT_MODULE_1;

	return (cap_charge + cap_delay - NPCX_TCRA(mdl));
}

static void cec_isr(void)
{
	int port = cec_port;
	int mdl = NPCX_MFT_MODULE_1;
	uint8_t events;

	/* Retrieve events NPCX_TECTRL_TAXND */
	events = GET_FIELD(NPCX_TECTRL(mdl), FIELD(0, 4));

	if (events & BIT(NPCX_TECTRL_TAPND)) {
		/* Capture event */
		cec_event_cap(port);
	} else {
		/*
		 * Capture timeout
		 * We only care about this if the capture event is not
		 * happening, since we will get both events in the
		 * edge-trigger case
		 */
		if (events & BIT(NPCX_TECTRL_TCPND))
			cec_event_timeout(port);
	}

	/* Software interrupt, a transfer has been initiated from AP */
	if (sw_interrupt > 0) {
		sw_interrupt = 0;
		cec_event_tx(port);
	}

	/* Clear handled events */
	SET_FIELD(NPCX_TECLR(mdl), FIELD(0, 4), events);
}
DECLARE_IRQ(NPCX_IRQ_MFT_1, cec_isr, 4);

void cec_trigger_send(int port)
{
	/* Elevate to interrupt context */
	sw_interrupt = 1;
	task_trigger_irq(NPCX_IRQ_MFT_1);
}

void cec_enable_timer(int port)
{
	int mdl = NPCX_MFT_MODULE_1;

	/* Configure GPIO40/TA1 as capture timer input (TA1) */
	CLEAR_BIT(NPCX_DEVALT(0xC), NPCX_DEVALTC_TA1_SL2);
	SET_BIT(NPCX_DEVALT(3), NPCX_DEVALT3_TA1_SL1);

	/* Enable timer interrupts */
	SET_BIT(NPCX_TIEN(mdl), NPCX_TIEN_TAIEN);

	/* Enable multifunction timer interrupt */
	task_enable_irq(NPCX_IRQ_MFT_1);
}

void cec_disable_timer(int port)
{
	int mdl = NPCX_MFT_MODULE_1;

	/* Disable timer interrupts */
	CLEAR_BIT(NPCX_TIEN(mdl), NPCX_TIEN_TAIEN);

	cec_tmr_cap_stop(port);

	task_disable_irq(NPCX_IRQ_MFT_1);

	/* Configure GPIO40/TA1 back to GPIO */
	CLEAR_BIT(NPCX_DEVALT(3), NPCX_DEVALT3_TA1_SL1);
	SET_BIT(NPCX_DEVALT(0xC), NPCX_DEVALTC_TA1_SL2);

	cap_charge = 0;
	cap_delay = 0;
}

void cec_init_timer(int port)
{
	int mdl = NPCX_MFT_MODULE_1;

	if (port < 0 || port >= CEC_PORT_COUNT)
		CPRINTS("CEC ERR: Invalid port # %d", port);

	cec_port = port;

	/* APB1 is the clock we base the timers on */
	apb1_freq_div_10k = clock_get_apb1_freq() / 10000;

	/* Ensure Multi-Function timer is powered up. */
	CLEAR_BIT(NPCX_PWDWN_CTL(mdl), NPCX_PWDWN_CTL1_MFT1_PD);

	/* Mode 2 - Dual-input capture */
	SET_FIELD(NPCX_TMCTRL(mdl), NPCX_TMCTRL_MDSEL_FIELD, NPCX_MFT_MDSEL_2);

	/* Enable capture TCNT1 into TCRA and preset TCNT1. */
	SET_BIT(NPCX_TMCTRL(mdl), NPCX_TMCTRL_TAEN);
}
