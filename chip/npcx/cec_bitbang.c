/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cec.h"
#include "cec_bitbang_chip.h"
#include "clock_chip.h"
#include "console.h"
#include "driver/cec/bitbang.h"
#include "fan_chip.h"
#include "registers.h"
#include "task.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_CEC, format, ##args)
#define CPRINTS(format, args...) cprints(CC_CEC, format, ##args)

#ifdef CONFIG_CEC_DEBUG
#define DEBUG_CPRINTF(format, args...) cprintf(CC_CEC, format, ##args)
#define DEBUG_CPRINTS(format, args...) cprints(CC_CEC, format, ##args)
#else
#define DEBUG_CPRINTF(...)
#define DEBUG_CPRINTS(...)
#endif

struct npcx_tmr_flags {
	uint8_t interrupt;
	uint8_t pending;
	uint8_t clear_pending;
};

struct npcx_cec_timer_cfg {
	struct npcx_tmr_flags capture;
	struct npcx_tmr_flags underflow;
	uint8_t clock_select;
	uint8_t edge;
	uint8_t enable;
};

struct npcx_cec_timer {
	volatile uint16_t *tcr;
	volatile uint16_t *tcnt;
};

struct npcx_cec_port {
	/*
	 * Time between interrupt triggered and the next timer was
	 * set when measuring pulse width
	 */
	int cap_delay;

	/* Value charged into the capture timer on last capture start */
	int cap_charge;

	/* Software generated interrupt triggers the send logic */
	uint8_t sw_interrupt;
};

/* TODO(b/296813751): Implement a common data structure for CEC drivers */
static struct npcx_cec_port npcx_cec_port[CEC_PORT_COUNT];

static struct npcx_cec_timer npcx_cec_timer[NPCX_CEC_BITBANG_TIMER_COUNT] = {
	{
		.tcr = &NPCX_TCRA(NPCX_MFT_MODULE_1),
		.tcnt = &NPCX_TCNT1(NPCX_MFT_MODULE_1),
	},
	{
		.tcr = &NPCX_TCRB(NPCX_MFT_MODULE_1),
		.tcnt = &NPCX_TCNT2(NPCX_MFT_MODULE_1),
	},
};

static const struct npcx_cec_timer_cfg
	npcx_cec_timer_cfg[NPCX_CEC_BITBANG_TIMER_COUNT] = {
		{
			/* Source A is capture and source C is underflow */
			.capture = {
				.interrupt = NPCX_TIEN_TAIEN,
				.pending = NPCX_TECTRL_TAPND,
				.clear_pending = NPCX_TECLR_TACLR,
			},
			.underflow = {
				.interrupt = NPCX_TIEN_TCIEN,
				.pending = NPCX_TECTRL_TCPND,
				.clear_pending = NPCX_TECLR_TCCLR,

			},
			.edge = NPCX_TMCTRL_TAEDG,
			.clock_select = 1,
			.enable = NPCX_TMCTRL_TAEN,
		},
		{
			/* Source B is capture and source D is underflow */
			{
				/* Capture */
				.interrupt = NPCX_TIEN_TBIEN,
				.pending = NPCX_TECTRL_TBPND,
				.clear_pending = NPCX_TECLR_TBCLR,
			},
			{
				/* Underflow */
				.interrupt = NPCX_TIEN_TDIEN,
				.pending = NPCX_TECTRL_TDPND,
				.clear_pending = NPCX_TECLR_TDCLR,

			},
			.edge = NPCX_TMCTRL_TBEDG,
			.clock_select = 2,
			.enable = NPCX_TMCTRL_TBEN,
		},
	};

/* APB1 frequency. Store divided by 10k to avoid some runtime divisions */
uint32_t apb1_freq_div_10k;

void cec_tmr_cap_start(int port, enum cec_cap_edge edge, int timeout)
{
	const int mdl = NPCX_MFT_MODULE_1;
	struct npcx_cec_port *cec_port = &npcx_cec_port[port];
	const struct bitbang_cec_config *drv_config =
		cec_config[port].drv_config;
	struct npcx_cec_timer *timer = &npcx_cec_timer[drv_config->timer];
	const struct npcx_cec_timer_cfg *timer_cfg =
		&npcx_cec_timer_cfg[drv_config->timer];
	const struct npcx_tmr_flags *capture_flags = &timer_cfg->capture;
	const struct npcx_tmr_flags *underflow_flags = &timer_cfg->underflow;

	if (edge == CEC_CAP_EDGE_NONE) {
		/*
		 * If edge is NONE, disable capture interrupts and wait for a
		 * timeout only.
		 */
		CLEAR_BIT(NPCX_TIEN(mdl), capture_flags->interrupt);
	} else {
		/* Select edge to trigger capture on */
		UPDATE_BIT(NPCX_TMCTRL(mdl), timer_cfg->edge,
			   edge == CEC_CAP_EDGE_RISING);
		SET_BIT(NPCX_TIEN(mdl), capture_flags->interrupt);
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
		cec_port->cap_delay = (0xffff - *timer->tcnt);
		cec_port->cap_charge = timeout - cec_port->cap_delay;
		*timer->tcnt = cec_port->cap_charge;
		SET_BIT(NPCX_TIEN(mdl), underflow_flags->interrupt);
	} else {
		CLEAR_BIT(NPCX_TIEN(mdl), underflow_flags->interrupt);
		*timer->tcnt = 0;
	}

	/* Clear out old events */
	SET_BIT(NPCX_TECLR(mdl), capture_flags->clear_pending);
	SET_BIT(NPCX_TECLR(mdl), underflow_flags->clear_pending);
	*timer->tcr = 0;

	/* Start the capture timer */
	if (timer_cfg->clock_select == 1)
		SET_FIELD(NPCX_TCKC(mdl), NPCX_TCKC_C1CSEL_FIELD, 1);
	else if (timer_cfg->clock_select == 2)
		SET_FIELD(NPCX_TCKC(mdl), NPCX_TCKC_C2CSEL_FIELD, 1);
}

void cec_tmr_cap_stop(int port)
{
	const int mdl = NPCX_MFT_MODULE_1;
	const struct bitbang_cec_config *drv_config =
		cec_config[port].drv_config;
	const struct npcx_cec_timer_cfg *timer_cfg =
		&npcx_cec_timer_cfg[drv_config->timer];

	CLEAR_BIT(NPCX_TIEN(mdl), NPCX_TIEN_TCIEN);
	if (timer_cfg->clock_select == 1)
		SET_FIELD(NPCX_TCKC(mdl), NPCX_TCKC_C1CSEL_FIELD, 0);
	else if (timer_cfg->clock_select == 2)
		SET_FIELD(NPCX_TCKC(mdl), NPCX_TCKC_C2CSEL_FIELD, 0);
}

int cec_tmr_cap_get(int port)
{
	const struct npcx_cec_port *cec_port = &npcx_cec_port[port];
	const struct bitbang_cec_config *drv_config =
		cec_config[port].drv_config;
	struct npcx_cec_timer *timer = &npcx_cec_timer[drv_config->timer];

	return (cec_port->cap_charge + cec_port->cap_delay - *timer->tcr);
}

/*
 * Nothing to do. cec_tmr_cap_start() already enables/disables capture
 * interrupts based on the cap_edge.
 */
void cec_debounce_enable(int port)
{
}

void cec_debounce_disable(int port)
{
}

static void cec_isr(void)
{
	const int mdl = NPCX_MFT_MODULE_1;
	uint8_t events;

	/* Retrieve events NPCX_TECTRL_TAXND */
	events = GET_FIELD(NPCX_TECTRL(mdl), FIELD(0, 4));

	for (int port = 0; port < CEC_PORT_COUNT; port++) {
		const struct bitbang_cec_config *drv_config =
			cec_config[port].drv_config;
		const struct npcx_cec_timer_cfg *timer_cfg =
			&npcx_cec_timer_cfg[drv_config->timer];
		if (events & BIT(timer_cfg->capture.pending)) {
			/* Capture event */
			cec_event_cap(port);
		} else if (events & BIT(timer_cfg->underflow.pending)) {
			/*
			 * Capture timeout
			 * We only care about this if the capture event is not
			 * happening, since we will get both events in the
			 * edge-trigger case
			 */
			cec_event_timeout(port);
		}
	}

	/* Software interrupt, a transfer has been initiated from AP */
	for (int port = 0; port < CEC_PORT_COUNT; port++) {
		if (npcx_cec_port[port].sw_interrupt > 0) {
			npcx_cec_port[port].sw_interrupt = 0;
			cec_event_tx(port);
		}
	}

	/* Clear handled events */
	SET_FIELD(NPCX_TECLR(mdl), FIELD(0, 4), events);
}
DECLARE_IRQ(NPCX_IRQ_MFT_1, cec_isr, 4);

void cec_trigger_send(int port)
{
	/* Elevate to interrupt context */
	npcx_cec_port[port].sw_interrupt = 1;
	task_trigger_irq(NPCX_IRQ_MFT_1);
}

void cec_enable_timer(int port)
{
	const int mdl = NPCX_MFT_MODULE_1;
	const struct bitbang_cec_config *drv_config =
		cec_config[port].drv_config;

	if (drv_config->timer == NPCX_CEC_BITBANG_TIMER_A) {
		/* Configure GPIO40/TA1 as capture timer input (TA1) */
		CLEAR_BIT(NPCX_DEVALT(0xC), NPCX_DEVALTC_TA1_SL2);
		SET_BIT(NPCX_DEVALT(3), NPCX_DEVALT3_TA1_SL1);
	} else if (drv_config->timer == NPCX_CEC_BITBANG_TIMER_B) {
		/* Configure GPIOD3/TB1 as capture timer input (TB1) */
		CLEAR_BIT(NPCX_DEVALT(3), NPCX_DEVALT3_TB1_SL1);
		SET_BIT(NPCX_DEVALT(0xC), NPCX_DEVALTC_TB1_SL2);
	}

	/* Enable timer interrupts */
	SET_BIT(NPCX_TIEN(mdl),
		npcx_cec_timer_cfg[drv_config->timer].capture.interrupt);

	/* Enable multifunction timer interrupt */
	task_enable_irq(NPCX_IRQ_MFT_1);
}
void cec_disable_timer(int port)
{
	const int mdl = NPCX_MFT_MODULE_1;
	struct npcx_cec_port *cec_port = &npcx_cec_port[port];
	const struct bitbang_cec_config *drv_config =
		cec_config[port].drv_config;
	const struct npcx_cec_timer_cfg *timer_cfg =
		&npcx_cec_timer_cfg[drv_config->timer];

	/* Disable timer interrupts */
	CLEAR_BIT(NPCX_TIEN(mdl), timer_cfg->capture.interrupt);

	cec_tmr_cap_stop(port);

	if (drv_config->timer == NPCX_CEC_BITBANG_TIMER_A) {
		/* Configure GPIO40/TA1 back to GPIO */
		CLEAR_BIT(NPCX_DEVALT(3), NPCX_DEVALT3_TA1_SL1);
	} else if (drv_config->timer == NPCX_CEC_BITBANG_TIMER_B) {
		/* Configure GPIOD3/TB1 back to GPIO */
		CLEAR_BIT(NPCX_DEVALT(0xC), NPCX_DEVALTC_TB1_SL2);
	}

	cec_port->cap_charge = 0;
	cec_port->cap_delay = 0;

	/* If there is no enabled timers, turn off interrupts */
	for (int p = 0; p < CEC_PORT_COUNT; p++) {
		drv_config = cec_config[p].drv_config;
		if (IS_BIT_SET(NPCX_TIEN(mdl),
			       npcx_cec_timer_cfg[drv_config->timer]
				       .capture.interrupt))
			return;
	}
	task_disable_irq(NPCX_IRQ_MFT_1);
}

void cec_init_timer(int port)
{
	const int mdl = NPCX_MFT_MODULE_1;
	const struct bitbang_cec_config *drv_config;

	if (port < 0 || port >= CEC_PORT_COUNT) {
		CPRINTS("CEC ERR: Invalid port # %d", port);
		return;
	}

	if (port >= NPCX_CEC_BITBANG_TIMER_COUNT) {
		CPRINTS("CEC ERR: NPCX does not support port # %d", port);
		return;
	}

	drv_config = cec_config[port].drv_config;
	if (drv_config->timer < 0 ||
	    drv_config->timer >= NPCX_CEC_BITBANG_TIMER_COUNT) {
		CPRINTS("CEC%d ERR: Invalid timer # %d", port,
			drv_config->timer);
		return;
	}

	/* APB1 is the clock we base the timers on */
	apb1_freq_div_10k = clock_get_apb1_freq() / 10000;

	/* Ensure Multi-Function timer is powered up. */
	CLEAR_BIT(NPCX_PWDWN_CTL(mdl), NPCX_PWDWN_CTL1_MFT1_PD);

	/* Mode 5 - Dual-independent input capture */
	SET_FIELD(NPCX_TMCTRL(mdl), NPCX_TMCTRL_MDSEL_FIELD, NPCX_MFT_MDSEL_5);

	/* Enable capture TCNTx into TCRx and preset TCNTx. */
	SET_BIT(NPCX_TMCTRL(mdl), npcx_cec_timer_cfg[drv_config->timer].enable);
}
