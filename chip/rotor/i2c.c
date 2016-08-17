/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* I2C driver for Rotor MCU */

/* Console output macros */
#define CPUTS(outstr) cputs(CC_I2C, outstr)
#define CPRINTS(format, args...) cprints(CC_I2C, format, ## args)

/* Timing table. */
/* TODO(aaboagye): Add entries once you figure out the APLL0 src frequency. */
enum i2c_input_clk {
	I2C_CLK_SRC_25MHZ,
	I2C_CLK_SRC_8MHZ, /* TODO(aaboagye): This is the ANA_GRP DRO,
			   * but I'm not sure of the exact frequency.
			   * Clock diagram shows a range from 8-24MHz
			   */
	I2C_CLK_SRC_32KHZ,
	NUM_I2C_SRC_CLKS
};

struct i2c_timing_t {
	uint16_t t_high;
	uint16_t t_low;
};

struct i2c_timing_map_t {
	const struct i2c_timing_t *times;
};

/*
 * Standard mode Minimum times according to spec:
 *   t_high = 4.0 us
 *   t_low = 4.7 us
 */
static const struct i2c_timing_t standard_mode_timings[NUM_I2C_SRC_CLKS] = {
	/* 25MHz */
	{
		.t_high = 100,
		.t_low = 118,
	},

	/* 8MHz */
	{
		.t_high = 32,
		.t_low = 38,
	},

	/* 32KHz */
	{
		.t_high = 1,
		.t_low = 1,
	},
};

/*
 * Fast mode minimum times according to spec:
 *   t_high = 0.6 us
 *   t_low = 1.3 us
 */
static const struct i2c_timing_t fast_mode_timings[NUM_I2C_SRC_CLKS] = {
	/* 25MHz */
	{
		.t_high = 15,
		.t_low = 33,
	},

	/* 8MHz */
	{
		.t_high = 5,
		.t_low = 11,
	},

	/* 32KHz */
	{
		.t_high = 1,
		.t_low = 1,
	},
};

/*
 * Fast mode plus minimum times according to spec:
 *   t_high = 0.26 us
 *   t_low = 0.5 us
 */
static const struct i2c_timing_t fast_mode_plus_timings[NUM_I2C_SRC_CLKS] = {
	/* 25MHz */
	{
		.t_high = 7,
		.t_low = 13,
	},

	/* 8MHz */
	{
		.t_high = 3,
		.t_low = 4,
	},

	/* 32KHz */
	{
		.t_high = 1,
		.t_low = 1,
	},
};

static const struct i2c_timing_map_t timing_settings[I2C_FREQ_COUNT] = {
	{
		.times = fast_mode_plus_timings,
	},

	{
		.times = fast_mode_timings,
	},

	{
		.times = standard_mode_timings,
	},
};


/* Task waiting on port, or TASK_ID_INVALID if none. */
static volatile int task_waiting[I2C_PORT_COUNT] = { TASK_ID_INVALID };

/**
 * Dumps some i2c regs for debugging.
 *
 * @params port	The port for which regs to dump.
 */
static void dump_regs(int port)
{
	CPRINTS("I2C%d regs", port);
	CPRINTS("IC_TAR:		%08X", ROTOR_MCU_I2C_TAR(port));
	CPRINTS("IC_INTR_MASK:		%08X", ROTOR_MCU_I2C_INTR_MASK(port));
	CPRINTS("IC_INTR_STAT:		%08X", ROTOR_MCU_I2C_INTR_STAT(port));
	CPRINTS("IC_RAW_INTR_STAT:	%08X",
		ROTOR_MCU_I2C_RAW_INTR_STAT(port));
	CPRINTS("IC_STATUS:		%08X", ROTOR_MCU_I2C_STATUS(port));
	CPRINTS("IC_TX_ABRT_SRC:	%08X", ROTOR_MCU_I2C_TX_ABRT_SRC(port));
}

static int command_i2cdump(int argc, char **argv)
{
	int port;
	char *e;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	port = strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1;

	if ((port < 0) || (port >= I2C_PORT_COUNT))
		return EC_ERROR_PARAM1;

	dump_regs(port);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(i2cdump, command_i2cdump, NULL, NULL, NULL);

/*
 * TODO(aaboagye): Resurrect this once you figure out the clock issues.
 *
 * I was running into transfer aborts because the transactions were taking much
 * longer than expected.  Hacked this out so I could make progress.
 */
#if 0
/**
 * Abort the current transaction.
 *
 * The controller will send a STOP condition and flush the TX FIFO.
 *
 * @param port	The port where you wish to abort the transaction.
 */
static void abort_transfer(int port)
{
	int evt;

	/* Unmask the M_TX_ABRT interrupt. */
	ROTOR_MCU_I2C_INTR_MASK(port) = ROTOR_MCU_I2C_M_TX_ABRT;

	/* Issue the abort. */
	ROTOR_MCU_I2C_ENABLE(port) |= ROTOR_MCU_I2C_ABORT;

	/* Wait for the interrupt to fire. */
	evt = task_wait_event_mask(TASK_EVENT_I2C_IDLE, 500);
	if (evt & TASK_EVENT_TIMER)
		CPRINTS("i2c: timed out waiting for abort interrupt.");
	else
		CPRINTS("i2c xfer abort.");

	/* Mask the M_TX_ABRT interrupt. */
	ROTOR_MCU_I2C_INTR_MASK(port) = 0;
}
#endif /* 0 */

/**
 * Disable the I2C port.
 *
 * @param port	The port which you wish to disable.
 * @return EC_SUCCESS on successfully disabling the port, non-zero otherwise.
 */
static int disable_i2c(int port)
{
	uint8_t timeout = 50;

	/* Check if the hardware is already shutdown. */
	if (!(ROTOR_MCU_I2C_ENABLE_STATUS(port) & ROTOR_MCU_I2C_IC_EN))
		return EC_SUCCESS;

	/* Try disabling the port. */
	ROTOR_MCU_I2C_ENABLE(port) &= ~ROTOR_MCU_I2C_EN;

	/* Check to see that the hardware actually shuts down. */
	while (ROTOR_MCU_I2C_ENABLE_STATUS(port) & ROTOR_MCU_I2C_IC_EN) {
		usleep(10);
		timeout--;

		if (timeout == 0)
			return EC_ERROR_TIMEOUT;
	};

	return EC_SUCCESS;
}

/**
 * Wait until the byte has been popped from the TX FIFO.
 *
 * This interrupt is automatically cleared by hardware when the buffer level
 * goes above the threshold (set to one element).
 *
 * @param port		The i2c port to wait for.
 * @param timeout	The timeout in microseconds.
 */
static int wait_byte_done(int port, int timeout)
{
	uint32_t events;

	/* Unmask the TX_EMPTY interrupt. */
	task_waiting[port] = task_get_current();
	ROTOR_MCU_I2C_INTR_MASK(port) |= ROTOR_MCU_I2C_M_TX_EMPTY;

	/* Wait until the interrupt fires. */
	events = task_wait_event_mask(TASK_EVENT_I2C_IDLE, timeout);
	task_waiting[port] = TASK_ID_INVALID;

	return (events & TASK_EVENT_I2C_IDLE) ? EC_SUCCESS : EC_ERROR_TIMEOUT;
}

/**
 * Wait until the byte has been inserted to the RX FIFO.
 *
 * Since the RX transmission level is set to only 1 element, the RX_FULL
 * interrupt should fire when there's at least 1 new byte to read.  This
 * interrupt is automatically cleared by hardware when the buffer level goes
 * below the threshold (set to one element).
 *
 * @param port		The i2c port to wait for.
 * @param timeout	The timeout in microseconds.
 */
static int wait_byte_ready(int port, int timeout)
{
	uint32_t events;

	/* Unmask the RX_FULL interrupt. */
	task_waiting[port] = task_get_current();
	ROTOR_MCU_I2C_INTR_MASK(port) |= ROTOR_MCU_I2C_M_RX_FULL;

	/* Wait until the interrupt fires. */
	events = task_wait_event_mask(TASK_EVENT_I2C_IDLE, timeout);
	task_waiting[port] = TASK_ID_INVALID;

	return (events & TASK_EVENT_I2C_IDLE) ? EC_SUCCESS : EC_ERROR_TIMEOUT;
}

int chip_i2c_xfer(int port, int slave_addr, const uint8_t *out, int out_size,
		  uint8_t *in, int in_size, int flags)
{
	int i;
	int rv;
	int val;
	uint64_t task_timeout = get_time().val + I2C_TIMEOUT_DEFAULT_US;
	/* Check if there's anything we actually have to do. */
	if (!in_size && !out_size)
		return EC_SUCCESS;

	/* Make sure we're in a good state to start. */
	if ((flags & I2C_XFER_START) &&
	    i2c_get_line_levels(port) != I2C_LINE_IDLE) {
		CPRINTS("I2C%d Addr:%02X bad status SCL=%d, SDA=%d",
			port,
			slave_addr,
			i2c_get_line_levels(port) & I2C_LINE_SCL_HIGH,
			i2c_get_line_levels(port) & I2C_LINE_SDA_HIGH ? 1 : 0);

		/* Attempt to unwedge the port. */
		i2c_unwedge(port);
	}

	/* Set the slave address. */
	ROTOR_MCU_I2C_TAR(port) = (slave_addr >> 1) & 0xFF;

	/*
	 * Placing data into the TX FIFO causes the i2c block to generate a
	 * START condition on the bus.
	 */
	for (i = 0; i < out_size; i++) {
		/* Issue a STOP bit if this is the last byte. */
		if ((i == (out_size-1)) && (flags & I2C_XFER_STOP))
			ROTOR_MCU_I2C_DATA_CMD(port) |=
				(ROTOR_MCU_I2C_STOP | out[i]);
		else
			ROTOR_MCU_I2C_DATA_CMD(port) = out[i];

		/* Wait until byte popped from TX FIFO. */
		rv = wait_byte_done(port, task_timeout - get_time().val);
		if (rv != EC_SUCCESS) {
			/* Abort the transaction. */
			/* dump_regs(port); */
			return EC_ERROR_TIMEOUT;
		}
	}

	for (i = 0; i < in_size; i++) {
		/*
		 * In order for the i2c block to continue acknowledging reads, a
		 * read command must be written for every byte that is to be
		 * received.
		 */
		val = ROTOR_MCU_I2C_RD_CMD;

		/* Issue a RESTART since direction is changing. */
		if (i == 0)
			val |= ROTOR_MCU_I2C_RESTART;


		/* Issue a STOP if this is the last byte. */
		if ((i == (in_size-1)) && (flags & I2C_XFER_STOP))
			val |= ROTOR_MCU_I2C_STOP;

		ROTOR_MCU_I2C_DATA_CMD(port) = val;

		/* Wait for RX_FULL interrupt. */
		rv = wait_byte_ready(port, task_timeout - get_time().val);
		if (rv != EC_SUCCESS)
			return EC_ERROR_TIMEOUT;

		/* Retrieve the byte from the RX FIFO. */
		in[i] = (ROTOR_MCU_I2C_DATA_CMD(port) & 0xFF);
	}

	task_waiting[port] = TASK_ID_INVALID;
	return EC_SUCCESS;
}

/**
 * Set up the port with the requested speeds.
 *
 * @param port	I2C port being configured.
 * @param freq	The desired operation speed of the port.
 */
static void set_port_speed(int port, enum i2c_freq freq)
{
	enum i2c_input_clk src_clk;
	const struct i2c_timing_t *timings;

	/* Determine the current i2c clock source .*/
	switch ((ROTOR_MCU_I2C_REFCLKGEN(port) >> 24) & 0x3) {
	case 0: /* ANA_GRP XTAL */
		src_clk = I2C_CLK_SRC_25MHZ;
#ifdef CONFIG_BRINGUP
		CPRINTS("I2C clk src: 25MHz");
#endif /* defined(CONFIG_BRINGUP) */
		break;
	case 1: /* EXT 32KHz CLK */
		src_clk = I2C_CLK_SRC_32KHZ;
#ifdef CONFIG_BRINGUP
		CPRINTS("I2C clk src: 32KHz");
#endif /* defined(CONFIG_BRINGUP) */
		break;
	case 2: /* ANA_GRP DRO CLK */
		src_clk = I2C_CLK_SRC_8MHZ;
#ifdef CONFIG_BRINGUP
		CPRINTS("I2C clk src: 8MHz");
#endif /* defined(CONFIG_BRINGUP) */
		break;
	case 3: /* APLL0 CLK */
		/* Something like 589MHz?? */
#ifdef CONFIG_BRINGUP
		CPRINTS("I2C clk src: APLL0");
#endif /* defined(CONFIG_BRINGUP) */
		break;
	default:
		break;
	}

	/* Set the count registers for the appropriate timing. */
	timings = &timing_settings[freq].times[src_clk];

	switch (freq) {
	case I2C_FREQ_100KHZ: /* Standard Mode */
		ROTOR_MCU_I2C_CON(port) = ROTOR_MCU_I2C_SPEED_STD_MODE;
		ROTOR_MCU_I2C_SS_SCL_HCNT(port) = (timings->t_high & 0xFFFF);
		ROTOR_MCU_I2C_SS_SCL_LCNT(port) = (timings->t_low & 0xFFFF);
#ifdef CONFIG_BRINGUP
		CPRINTS("I2C%d speed 100KHz", port);
#endif /* defined(CONFIG_BRINGUP) */
		break;

	case I2C_FREQ_400KHZ: /* Fast Mode */
		ROTOR_MCU_I2C_CON(port) = ROTOR_MCU_I2C_SPEED_FAST_MODE;
		ROTOR_MCU_I2C_FS_SCL_HCNT(port) = (timings->t_high & 0xFFFF);
		ROTOR_MCU_I2C_FS_SCL_LCNT(port) = (timings->t_low & 0xFFFF);
#ifdef CONFIG_BRINGUP
		CPRINTS("I2C%d speed 400KHz", port);
#endif /* defined(CONFIG_BRINGUP) */
		break;

	case I2C_FREQ_1000KHZ: /* Fast Mode Plus */
		ROTOR_MCU_I2C_CON(port) = ROTOR_MCU_I2C_SPEED_HISPD_MODE;
		ROTOR_MCU_I2C_HS_SCL_HCNT(port) = (timings->t_high & 0xFFFF);
		ROTOR_MCU_I2C_HS_SCL_LCNT(port) = (timings->t_low & 0xFFFF);
#ifdef CONFIG_BRINGUP
		CPRINTS("I2C%d speed 1MHz", port);
#endif /* defined(CONFIG_BRINGUP) */
		break;
	default:
		break;
	};
}

/**
 * Initialize the specified I2C port.
 *
 * @param p	the I2C port.
 */
static void i2c_init_port(const struct i2c_port_t *p)
{
	int port = p->port;
	enum i2c_freq freq;

	/* Enable the clock for the port if necessary. */
	if (!(ROTOR_MCU_I2C_REFCLKGEN(port) & ROTOR_MCU_M4_BIST_CLKEN))
		ROTOR_MCU_I2C_REFCLKGEN(port) |= ROTOR_MCU_M4_BIST_CLKEN;

	/* Disable the I2C block to allow changes to certain registers. */
	disable_i2c(port);

	/*
	 * Mask all interrupts right now except for aborts so we can clear them.
	 * We'll unmask the ones we need as we go.
	 */
	ROTOR_MCU_I2C_INTR_MASK(port) = ROTOR_MCU_I2C_M_TX_ABRT;

	/* Set operation speed. */
	switch (p->kbps) {
	case 1000: /* Fast-mode Plus */
		freq = I2C_FREQ_1000KHZ;
		break;
	case 400: /* Fast-mode */
		freq = I2C_FREQ_400KHZ;
		break;
	case 100: /* Standard-mode */
		freq = I2C_FREQ_100KHZ;
		break;
	default: /* unknown speed, defaults to 100kBps */
		CPRINTS("I2C bad speed %d kBps.  Defaulting to 100kbps.",
			p->kbps);
		freq = I2C_FREQ_100KHZ;
	}
	/* TODO(aaboagye): Verify that the frequency is set correctly. */
	set_port_speed(port, freq);

	/*
	 * Configure as I2C master allowing RESTART conditions and using 7-bit
	 * addressing.  I2C_CON is initialized by the call to set_port_speed()
	 * above.
	 */
	ROTOR_MCU_I2C_CON(port) |= ROTOR_MCU_I2C_MASTER_MODE |
		ROTOR_MCU_I2C_IC_SLAVE_DISABLE |
		ROTOR_MCU_I2C_IC_RESTART_EN;

	/* Enable interrupts for the port. */
	task_enable_irq(ROTOR_MCU_IRQ_I2C_0 + port);

	/* Enable the port. */
	ROTOR_MCU_I2C_ENABLE(port) |= ROTOR_MCU_I2C_EN;
}

/**
 * Initialize the i2c module for all supported ports.
 */
static void i2c_init(void)
{
	const struct i2c_port_t *p = i2c_ports;
	int i;

	for (i = 0; i < i2c_ports_used; i++, p++)
		i2c_init_port(p);

	/* Configure the GPIO pins for I2C usage. */
	gpio_config_module(MODULE_I2C, 1);
}
DECLARE_HOOK(HOOK_INIT, i2c_init, HOOK_PRIO_INIT_I2C);

int i2c_get_line_levels(int port)
{
	return (i2c_raw_get_sda(port) ? I2C_LINE_SDA_HIGH : 0) |
		(i2c_raw_get_scl(port) ? I2C_LINE_SCL_HIGH : 0);
}

int i2c_raw_get_scl(int port)
{
	enum gpio_signal pin;

	if (get_scl_from_i2c_port(port, &pin) == EC_SUCCESS)
		return gpio_get_level(pin);

	/* If no SCL pin defined for this port, then return 1 to appear idle. */
	return 1;
}

int i2c_raw_get_sda(int port)
{
	enum gpio_signal pin;

	if (get_sda_from_i2c_port(port, &pin) == EC_SUCCESS)
		return gpio_get_level(pin);

	/* If no SDA pin defined for this port, then return 1 to appear idle. */
	return 1;
}

/**
 * Handle an interrupt on the specified port.
 *
 * @param port	I2C port generating interrupt
 */
static void handle_interrupt(int port)
{
	int waiting_task = task_waiting[port];

	/* Clear software clearable interrupt status. */
	if (ROTOR_MCU_I2C_CLR_INTR(port))
		;

	/* Clear TX aborts. */
	if (ROTOR_MCU_I2C_CLR_TX_ABRT(port))
		;

	/* If no task is waiting, just return. */
	if (waiting_task == TASK_ID_INVALID)
		return;

	/*
	 * If the TX_EMPTY is unmasked, let's mask it so we don't keep
	 * re-entering this IRQ handler.
	 */
	if ((ROTOR_MCU_I2C_INTR_MASK(port) & ROTOR_MCU_I2C_M_TX_EMPTY) &
	    (ROTOR_MCU_I2C_INTR_STAT(port) & ROTOR_MCU_I2C_M_TX_EMPTY))
		ROTOR_MCU_I2C_INTR_MASK(port) &= ~ROTOR_MCU_I2C_M_TX_EMPTY;

	/* Same for RX_FULL */
	if ((ROTOR_MCU_I2C_INTR_MASK(port) & ROTOR_MCU_I2C_M_RX_FULL) &
	    (ROTOR_MCU_I2C_INTR_STAT(port) & ROTOR_MCU_I2C_M_RX_FULL))
		ROTOR_MCU_I2C_INTR_MASK(port) &= ~ROTOR_MCU_I2C_M_RX_FULL;

	/* Wake up the task which was waiting for the interrupt. */
	task_set_event(waiting_task, TASK_EVENT_I2C_IDLE, 0);
}

void i2c0_interrupt(void) { handle_interrupt(0); }
void i2c1_interrupt(void) { handle_interrupt(1); }
void i2c2_interrupt(void) { handle_interrupt(2); }
void i2c3_interrupt(void) { handle_interrupt(3); }
void i2c4_interrupt(void) { handle_interrupt(4); }
void i2c5_interrupt(void) { handle_interrupt(5); }

DECLARE_IRQ(ROTOR_MCU_IRQ_I2C_0, i2c0_interrupt, 2);
DECLARE_IRQ(ROTOR_MCU_IRQ_I2C_1, i2c1_interrupt, 2);
DECLARE_IRQ(ROTOR_MCU_IRQ_I2C_2, i2c2_interrupt, 2);
DECLARE_IRQ(ROTOR_MCU_IRQ_I2C_3, i2c3_interrupt, 2);
DECLARE_IRQ(ROTOR_MCU_IRQ_I2C_4, i2c4_interrupt, 2);
DECLARE_IRQ(ROTOR_MCU_IRQ_I2C_5, i2c5_interrupt, 2);
