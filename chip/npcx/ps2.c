/*
 * Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* PS/2 module for Chrome EC */
#include "atomic.h"
#include "clock.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "ps2_chip.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_PS2, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_PS2, format, ##args)

#if !(DEBUG_PS2)
#define DEBUG_CPRINTS(...)
#define DEBUG_CPRINTF(...)
#else
#define DEBUG_CPRINTS(format, args...) cprints(CC_PS2, format, ##args)
#define DEBUG_CPRINTF(format, args...) cprintf(CC_PS2, format, ##args)
#endif

/*
 * Set WDAT3-0 and clear CLK3-0 in the PSOSIG register to
 * reset the shift mechanism.
 */
#define PS2_SHIFT_MECH_RESET 0x47

#define PS2_TRANSACTION_TIMEOUT (20 * MSEC)
#define PS2_BUSY_RETRY 10

enum ps2_input_debounce_cycle {
	PS2_IDB_1_CYCLE,
	PS2_IDB_2_CYCLE,
	PS2_IDB_4_CYCLE,
	PS2_IDB_8_CYCLE,
	PS2_IDB_16_CYCLE,
	PS2_IDB_32_CYCLE,
};

enum ps2_opr_mode {
	PS2_TX_MODE,
	PS2_RX_MODE,
};

struct ps2_data {
	/* PS/2 module operation mode */
	uint8_t opr_mode;
	/*
	 * The callback function to process data received from PS/2 device.
	 * Note: this is called in the PS/2 interrupt handler
	 */
	void (*rx_handler_cb)(uint8_t data);
};
static struct ps2_data ps2_ch_data[NPCX_PS2_CH_COUNT] = {
	[0 ...(NPCX_PS2_CH_COUNT - 1)] = { PS2_RX_MODE, NULL }
};

/*
 * Bitmap to record the enabled PS/2 channel by upper layer.
 * Only bit[7 and bit[5:3] are used
 * (i.e. the bit position of CLK3-0 in the PS2_PSOSIG register)
 */
static uint32_t channel_enabled_mask;
static struct mutex ps2_lock;
static volatile task_id_t task_waiting = TASK_ID_INVALID;

static void ps2_init(void)
{
	/* Disable the power down bit of PS/2 */
	clock_enable_peripheral(CGC_OFFSET_PS2, CGC_PS2_MASK,
				CGC_MODE_RUN | CGC_MODE_SLEEP);

	/* Disable shift mechanism and configure PS/2 to received mode. */
	NPCX_PS2_PSCON = 0x0;
	/* Set WDAT3-0 and clear CLK3-0 before enabling shift mechanism */
	NPCX_PS2_PSOSIG = PS2_SHIFT_MECH_RESET;

	/*
	 * PS/2 interrupt enable register
	 * [0] - : SOTIE   = 1: Start Of Transaction Interrupt Enable
	 * [1] - : EOTIE   = 1: End Of Transaction Interrupt Enable
	 * [4] - : WUE     = 1: Wake-Up Enable
	 * [7] - : CLK_SEL = 1: Select Free-Run clock as the basic clock
	 */
	NPCX_PS2_PSIEN = BIT(NPCX_PS2_PSIEN_SOTIE) | BIT(NPCX_PS2_PSIEN_EOTIE) |
			 BIT(NPCX_PS2_PSIEN_PS2_WUE) |
			 BIT(NPCX_PS2_PSIEN_PS2_CLK_SEL);

	/* Enable weak internal pull-up */
	SET_BIT(NPCX_PS2_PSCON, NPCX_PS2_PSCON_WPUED);
	/* Enable shift mechanism */
	SET_BIT(NPCX_PS2_PSCON, NPCX_PS2_PSCON_EN);

	/* Configure pins from GPIOs to PS/2 interface */
	gpio_config_module(MODULE_PS2, 1);
	task_enable_irq(NPCX_IRQ_PS2);
}
DECLARE_HOOK(HOOK_INIT, ps2_init, HOOK_PRIO_DEFAULT);

void ps2_enable_channel(int channel, int enable, void (*callback)(uint8_t data))
{
	if (channel >= NPCX_PS2_CH_COUNT) {
		CPRINTS("Err:PS/2 CH exceed %d", NPCX_PS2_CH_COUNT);
		return;
	}

	/*
	 * Disable the interrupt during changing the enabled channel mask to
	 * prevent from preemption
	 */
	interrupt_disable();
	if (enable) {
		ps2_ch_data[channel].rx_handler_cb = callback;
		channel_enabled_mask |= BIT(NPCX_PS2_PSOSIG_CLK(channel));
		/* Enable the relevant channel clock */
		SET_BIT(NPCX_PS2_PSOSIG, NPCX_PS2_PSOSIG_CLK(channel));
	} else {
		channel_enabled_mask &= ~BIT(NPCX_PS2_PSOSIG_CLK(channel));
		/* Disable the relevant channel clock */
		CLEAR_BIT(NPCX_PS2_PSOSIG, NPCX_PS2_PSOSIG_CLK(channel));
		ps2_ch_data[channel].rx_handler_cb = NULL;
	}
	interrupt_enable();
}

/* Check if the shift mechanism is busy */
static int ps2_is_busy(void)
{
	/*
	 * The driver pulls the CLK for non-active channels to low when Start
	 * bit is detected and pull the CLK of the active channel low after
	 * Stop bit detected. The EOT bit is set when Stop bit is detected,
	 * but both SOT and EOT are cleared when all CLKs are pull low
	 * (due to Shift Mechanism is reset)
	 */
	return (IS_BIT_SET(NPCX_PS2_PSTAT, NPCX_PS2_PSTAT_SOT) |
		IS_BIT_SET(NPCX_PS2_PSTAT, NPCX_PS2_PSTAT_EOT)) ?
		       1 :
		       0;
}

int ps2_transmit_byte(int channel, uint8_t data)
{
	int event;

	uint8_t busy_retry = PS2_BUSY_RETRY;

	if (channel >= NPCX_PS2_CH_COUNT) {
		CPRINTS("Err:PS/2 CH exceed %d", NPCX_PS2_CH_COUNT);
		return EC_ERROR_INVAL;
	}

	if (!(BIT(NPCX_PS2_PSOSIG_CLK(channel)) & channel_enabled_mask)) {
		CPRINTS("Err: PS/2 Tx w/o enabling CH");
		return EC_ERROR_INVAL;
	}

	mutex_lock(&ps2_lock);
	while (ps2_is_busy()) {
		crec_usleep(PS2_TRANSACTION_TIMEOUT);
		if (busy_retry == 0) {
			mutex_unlock(&ps2_lock);
			return EC_ERROR_BUSY;
		}
		busy_retry--;
	}

	task_waiting = task_get_current();
	ps2_ch_data[channel].opr_mode = PS2_TX_MODE;

	/* Set PS/2 in transmit mode */
	SET_BIT(NPCX_PS2_PSCON, NPCX_PS2_PSCON_XMT);
	/* Enable Start Of Transaction interrupt */
	SET_BIT(NPCX_PS2_PSIEN, NPCX_PS2_PSIEN_SOTIE);

	/* Reset the shift mechanism */
	NPCX_PS2_PSOSIG = PS2_SHIFT_MECH_RESET;
	/* Inhibit communication should last at least 100 micro-seconds */
	udelay(100);

	/* Write the data to be transmitted */
	NPCX_PS2_PSDAT = data;
	/* Apply the Request-to-send */
	CLEAR_BIT(NPCX_PS2_PSOSIG, NPCX_PS2_PSOSIG_WDAT(channel));
	SET_BIT(NPCX_PS2_PSOSIG, NPCX_PS2_PSOSIG_CLK(channel));

	/* Wait for interrupt */
	event = task_wait_event_mask(TASK_EVENT_PS2_DONE,
				     PS2_TRANSACTION_TIMEOUT);
	task_waiting = TASK_ID_INVALID;

	if (event == TASK_EVENT_TIMER) {
		task_disable_irq(NPCX_IRQ_PS2);
		CPRINTS("PS/2 Tx timeout");
		/* Reset the shift mechanism */
		NPCX_PS2_PSOSIG = PS2_SHIFT_MECH_RESET;
		/* Change the PS/2 module to receive mode */
		CLEAR_BIT(NPCX_PS2_PSCON, NPCX_PS2_PSCON_XMT);
		/* Restore the channel to Receive mode */
		ps2_ch_data[channel].opr_mode = PS2_RX_MODE;
		/*
		 * Restore the enabled channel according to channel_enabled_mask
		 */
		NPCX_PS2_PSOSIG |= channel_enabled_mask;
		task_enable_irq(NPCX_IRQ_PS2);
	}
	mutex_unlock(&ps2_lock);

	DEBUG_CPRINTF("Evt:0x%08x\n", event);
	return (event == TASK_EVENT_PS2_DONE) ? EC_SUCCESS : EC_ERROR_TIMEOUT;
}

static void ps2_stop_inactive_ch_clk(uint8_t active_ch)
{
	uint8_t mask;

	mask = ~NPCX_PS2_PSOSIG_CLK_MASK_ALL |
	       BIT(NPCX_PS2_PSOSIG_CLK(active_ch));
	NPCX_PS2_PSOSIG &= mask;
}

static int ps2_is_rx_error(uint8_t ch)
{
	uint8_t status;

	status = NPCX_PS2_PSTAT &
		 (BIT(NPCX_PS2_PSTAT_PERR) | BIT(NPCX_PS2_PSTAT_RFERR));
	if (status) {
		if (status & BIT(NPCX_PS2_PSTAT_PERR))
			CPRINTF("PS2 CH %d RX parity error\n", ch);
		if (status & BIT(NPCX_PS2_PSTAT_RFERR))
			CPRINTF("PS2 CH %d RX Frame error\n", ch);
		return 1;
	} else
		return 0;
}

static void ps2_int_handler(void)
{
	uint8_t active_ch;

	DEBUG_CPRINTS("PS2 INT");
	/*
	 * ACH = 1 : CHannel 0
	 * ACH = 2 : CHannel 1
	 * ACH = 4 : CHannel 2
	 * ACH = 5 : CHannel 3
	 */
	active_ch = GET_FIELD(NPCX_PS2_PSTAT, NPCX_PS2_PSTAT_ACH);
	active_ch = active_ch > 2 ? (active_ch - 2) : (active_ch - 1);
	DEBUG_CPRINTF("ACH:%0d-", active_ch);

	/*
	 * Inhibit PS/2 transaction of the other non-active channels by
	 * pulling down the clock signal
	 */
	ps2_stop_inactive_ch_clk(active_ch);

	/* PS/2 Start of Transaction */
	if (IS_BIT_SET(NPCX_PS2_PSTAT, NPCX_PS2_PSTAT_SOT) &&
	    IS_BIT_SET(NPCX_PS2_PSIEN, NPCX_PS2_PSIEN_SOTIE)) {
		DEBUG_CPRINTF("SOT-");
		/*
		 * Once set, SOT is not cleared until the shift mechanism
		 * is reset. Therefore, SOTIE should be cleared on the
		 * first occurrence of an SOT interrupt.
		 */
		CLEAR_BIT(NPCX_PS2_PSIEN, NPCX_PS2_PSIEN_SOTIE);
		/* PS/2 End of Transaction */
	} else if (IS_BIT_SET(NPCX_PS2_PSTAT, NPCX_PS2_PSTAT_EOT)) {
		DEBUG_CPRINTF("EOT-");
		CLEAR_BIT(NPCX_PS2_PSIEN, NPCX_PS2_PSIEN_EOTIE);

		/*
		 * Clear the CLK of active channel to reset
		 * the shift mechanism
		 */
		CLEAR_BIT(NPCX_PS2_PSOSIG, NPCX_PS2_PSOSIG_CLK(active_ch));

		if (ps2_ch_data[active_ch].opr_mode == PS2_TX_MODE) {
			/* Change the PS/2 module to receive mode */
			CLEAR_BIT(NPCX_PS2_PSCON, NPCX_PS2_PSCON_XMT);
			ps2_ch_data[active_ch].opr_mode = PS2_RX_MODE;
			task_set_event(task_waiting, TASK_EVENT_PS2_DONE);
		} else {
			if (!ps2_is_rx_error(active_ch)) {
				uint8_t data_read = NPCX_PS2_PSDAT;
				struct ps2_data *ps2_ptr =
					&ps2_ch_data[active_ch];

				DEBUG_CPRINTF("Recv:0x%02x", data_read);
				if (ps2_ptr->rx_handler_cb)
					ps2_ptr->rx_handler_cb(data_read);
			}
		}

		/* Restore the enabled channel */
		NPCX_PS2_PSOSIG |= channel_enabled_mask;
		/*
		 * Re-enable the Start Of Transaction interrupt when
		 * the shift mechanism is reset
		 */
		SET_BIT(NPCX_PS2_PSIEN, NPCX_PS2_PSIEN_SOTIE);
		SET_BIT(NPCX_PS2_PSIEN, NPCX_PS2_PSIEN_EOTIE);
	}
	DEBUG_CPRINTF("\n");
}
DECLARE_IRQ(NPCX_IRQ_PS2, ps2_int_handler, 5);

#ifdef CONFIG_CMD_PS2
static int command_ps2ench(int argc, const char **argv)
{
	uint8_t ch;
	uint8_t enable;
	char *e;

	ch = strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM2;

	enable = strtoi(argv[2], &e, 0);
	if (*e)
		return EC_ERROR_PARAM2;
	if (enable)
		ps2_enable_channel(ch, 1, NULL);
	else
		ps2_enable_channel(ch, 0, NULL);

	return 0;
}
DECLARE_CONSOLE_COMMAND(ps2ench, command_ps2ench, "ps2_ench channel 1|0",
			"Enable/Disable PS/2 channel");

static int command_ps2write(int argc, const char **argv)
{
	uint8_t ch, data;
	char *e;

	ch = strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM2;
	data = strtoi(argv[2], &e, 0);
	if (*e)
		return EC_ERROR_PARAM2;

	ps2_transmit_byte(ch, data);
	return 0;
}
DECLARE_CONSOLE_COMMAND(ps2write, command_ps2write, "ps2_write channel data",
			"Write data byte to PS/2 channel ");
#endif
