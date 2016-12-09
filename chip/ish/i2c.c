/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* I2C port module for ISH */

#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "registers.h"
#include "ish_i2c.h"
#include "task.h"
#include "timer.h"
#include "hwtimer.h"
#include "util.h"

#define CPUTS(outstr) cputs(CC_I2C, outstr)
#define CPRINTS(format, args...) cprints(CC_I2C, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_I2C, format, ## args)

#define I2C_FLAG_REPEATED_START_DISABLED	0
#define EVENT_FLAG_I2C_TIMEOUT			TASK_EVENT_CUSTOM(1 << 1)

static uint16_t default_hcnt_scl_100[] = {
	4000, 4420, 4920, 4400, 4000, 4000, 4300
};

static uint16_t default_lcnt_scl_100[] = {
	4720, 5180, 4990, 5333, 4700, 5200, 4950
};

static uint16_t default_hcnt_scl_400[] = {
	600, 820, 1120, 1066, 600, 600, 450
};

static uint16_t default_lcnt_scl_400[] = {
	1320, 1380, 1300, 1300, 1300, 1200, 1250
};

static uint16_t default_hcnt_scl_hs[] = { 160, 300, 160, 166, 175, 150, 162 };
static uint16_t default_lcnt_scl_hs[] = { 320, 340, 320, 325, 325, 300, 297 };

static uint8_t speed_val_arr[] = {
	STD_SPEED_VAL, FAST_SPEED_VAL, HIGH_SPEED_VAL };

static uint8_t bus_freq[ISH_I2C_PORT_COUNT] = {
	I2C_FREQ_120, I2C_FREQ_120, I2C_FREQ_120
};

static struct i2c_context i2c_ctxs[ISH_I2C_PORT_COUNT] = {
	{
		.bus = 0,
		.base = (uint32_t *) ISH_I2C0_BASE,
		.speed = I2C_SPEED_FAST,
	},
	{
		.bus = 1,
		.base = (uint32_t *) ISH_I2C1_BASE,
		.speed = I2C_SPEED_FAST,
	},
	{
		.bus = 2,
		.base = (uint32_t *) ISH_I2C2_BASE,
		.speed = I2C_SPEED_FAST,
	},
};

static struct i2c_bus_info board_config[ISH_I2C_PORT_COUNT] = {
	{
		.bus_id = 0,
		.std_speed.sda_hold = DEFAULT_SDA_HOLD,
		.fast_speed.sda_hold = DEFAULT_SDA_HOLD,
		.high_speed.sda_hold = DEFAULT_SDA_HOLD,
	},
	{
		.bus_id = 1,
		.std_speed.sda_hold = DEFAULT_SDA_HOLD,
		.fast_speed.sda_hold = DEFAULT_SDA_HOLD,
		.high_speed.sda_hold = DEFAULT_SDA_HOLD,
	},
	{
		.bus_id = 2,
		.std_speed.sda_hold = DEFAULT_SDA_HOLD,
		.fast_speed.sda_hold = DEFAULT_SDA_HOLD,
		.high_speed.sda_hold = DEFAULT_SDA_HOLD,
	 },
};

static inline void i2c_mmio_write(uint32_t *base, uint8_t offset,
				  uint32_t data)
{
	REG32((uint32_t) ((uint8_t *)base + offset)) = data;
}

static inline uint32_t i2c_mmio_read(uint32_t *base, uint8_t offset)
{
	return REG32((uint32_t) ((uint8_t *)base + offset));
}

static inline uint8_t i2c_read_byte(uint32_t *addr, uint8_t reg,
					  uint8_t offset)
{
	uint32_t ret = i2c_mmio_read(addr, reg) >> offset;

	return ret & 0xff;
}

static void i2c_intr_switch(uint32_t *base, int mode)
{
	switch (mode) {

	case ENABLE_WRITE_INT:
		i2c_mmio_write(base, IC_INTR_MASK, IC_INTR_WRITE_MASK_VAL);
		break;

	case ENABLE_READ_INT:
		i2c_mmio_write(base, IC_INTR_MASK, IC_INTR_READ_MASK_VAL);
		break;

	case DISABLE_INT:
		i2c_mmio_write(base, IC_INTR_MASK, 0);
		/* clear interrupts: TX_ABORT
		 * Because the DW_apb_i2c's TX FIFO is forced into a
		 * flushed/reset state whenever a TX_ABRT event occurs, it
		 * is necessary for software to release the DW_apb_i2c from
		 * this state by reading the IC_CLR_TX_ABRT register before
		 * attempting to write into the TX FIFO
		 */
		i2c_mmio_read(base, IC_CLR_TX_ABRT);
		/* STOP_DET */
		i2c_mmio_read(base, IC_CLR_STOP_DET);
		break;

	default:
		break;
	}
}

static void i2c_init_transaction(struct i2c_context *ctx,
				 uint8_t slave_addr, uint8_t flags)
{
	uint32_t con_value;
	uint32_t *base = ctx->base;
	struct i2c_bus_info *bus_info = &board_config[ctx->bus];
	uint32_t clk_in_val = clk_in[bus_freq[ctx->bus]];

	/* Convert 8-bit slave addrees to 7-bit for driver expectation*/
	slave_addr >>= 1;

	/* disable interrupts */
	i2c_intr_switch(base, DISABLE_INT);

	i2c_mmio_write(base, IC_ENABLE, IC_ENABLE_DISABLE);
	i2c_mmio_write(base, IC_TAR, (slave_addr << IC_TAR_OFFSET) |
		       TAR_SPECIAL_VAL | IC_10BITADDR_MASTER_VAL);

	/* set Clock SCL Count */
	switch (ctx->speed) {

	case I2C_SPEED_STD:
		i2c_mmio_write(base, IC_SS_SCL_HCNT,
			       NS_2_COUNTERS(bus_info->std_speed.hcnt,
					     clk_in_val));
		i2c_mmio_write(base, IC_SS_SCL_LCNT,
			       NS_2_COUNTERS(bus_info->std_speed.lcnt,
					     clk_in_val));
		i2c_mmio_write(base, IC_SDA_HOLD,
			       NS_2_COUNTERS(bus_info->std_speed.sda_hold,
					     clk_in_val));
		break;

	case I2C_SPEED_FAST:
		i2c_mmio_write(base, IC_FS_SCL_HCNT,
			       NS_2_COUNTERS(bus_info->fast_speed.hcnt,
					     clk_in_val));
		i2c_mmio_write(base, IC_FS_SCL_LCNT,
			       NS_2_COUNTERS(bus_info->fast_speed.lcnt,
					     clk_in_val));
		i2c_mmio_write(base, IC_SDA_HOLD,
			       NS_2_COUNTERS(bus_info->fast_speed.sda_hold,
					     clk_in_val));
		break;

	case I2C_SPEED_HIGH:
		i2c_mmio_write(base, IC_HS_SCL_HCNT,
			       NS_2_COUNTERS(bus_info->high_speed.hcnt,
					     clk_in_val));
		i2c_mmio_write(base, IC_HS_SCL_LCNT,
			       NS_2_COUNTERS(bus_info->high_speed.lcnt,
					     clk_in_val));
		i2c_mmio_write(base, IC_SDA_HOLD,
			       NS_2_COUNTERS(bus_info->high_speed.sda_hold,
					     clk_in_val));

		i2c_mmio_write(base, IC_FS_SCL_HCNT,
			       NS_2_COUNTERS(bus_info->fast_speed.hcnt,
					     clk_in_val));
		i2c_mmio_write(base, IC_FS_SCL_LCNT,
			       NS_2_COUNTERS(bus_info->fast_speed.lcnt,
					     clk_in_val));
		break;

	default:
		break;
	}

	/* in SPT HW we need to sync between I2C clock and data signals */
	con_value = i2c_mmio_read(base, IC_CON);

	if (flags & I2C_FLAG_REPEATED_START_DISABLED)
		con_value &= ~IC_RESTART_EN_VAL;
	else
		con_value |= IC_RESTART_EN_VAL;

	i2c_mmio_write(base, IC_CON, con_value);
	i2c_mmio_write(base, IC_FS_SPKLEN, spkln[bus_freq[ctx->bus]]);
	i2c_mmio_write(base, IC_HS_SPKLEN, spkln[bus_freq[ctx->bus]]);
	i2c_mmio_write(base, IC_ENABLE, IC_ENABLE_ENABLE);
}

static void i2c_write_buffer(uint32_t *base, uint8_t len,
			     const uint8_t *buffer, ssize_t *cur_index,
			     ssize_t total_len)
{
	int i;
	uint16_t out;

	for (i = 0; i < len; i++) {

		++(*cur_index);
		out = (buffer[i] << DATA_CMD_DAT_OFFSET) | DATA_CMD_WRITE_VAL;

		if (*cur_index == total_len)
			out |= DATA_CMD_STOP_VAL;

		i2c_mmio_write(base, IC_DATA_CMD, out);
	}
}

static void i2c_write_read_commands(uint32_t *base, uint8_t len)
{
	int i;

	for (i = 0; i < len - 1; i++)
		i2c_mmio_write(base, IC_DATA_CMD, DATA_CMD_READ_VAL);

	i2c_mmio_write(base, IC_DATA_CMD,
		       DATA_CMD_READ_VAL | DATA_CMD_STOP_VAL);
}

int chip_i2c_xfer(int port, int slave_addr, const uint8_t *out, int out_size,
		  uint8_t *in, int in_size, int flags)
{
	int i, is_read = 0;
	ssize_t total_len;
	uint64_t expire_ts;
	struct i2c_context *ctx;
	ssize_t curr_index = 0;

	if (out_size == 0 && in_size == 0)
		return EC_SUCCESS;

	if (in_size > 0)
		is_read = 1;

	ctx = &i2c_ctxs[port];
	ctx->error_flag = 0;

	total_len = is_read ? (1 + in_size) : out_size;

	i2c_init_transaction(ctx, slave_addr, flags);

	/* Write device id */
	i2c_write_buffer(ctx->base, 1, out, &curr_index, total_len);

	/* Write W data */
	i2c_write_buffer(ctx->base, (is_read ? 0 : out_size - 1),
			 (is_read ? NULL : out + 1),
			 &curr_index, total_len);

	if (is_read) {
		/* Write R commands */
		i2c_write_read_commands(ctx->base, in_size);

		/* Set rx_theshold */
		i2c_mmio_write(ctx->base, IC_RX_TL, in_size - 1);
	}

	/* Enable interrupts */
	i2c_intr_switch(ctx->base,
			is_read ? ENABLE_READ_INT : ENABLE_WRITE_INT);

	/* Wait for interrupt */
	ctx->wait_task_id = task_get_current();
	task_wait_event_mask(EVENT_FLAG_I2C_TIMEOUT, -1);

	if ((ctx->interrupts & M_TX_ABRT) == 0) {
		if (is_read) {
			/* read data */
			for (i = 0; i < in_size; i++)
				in[i] = i2c_read_byte(ctx->base,
						IC_DATA_CMD, 0);
		}

	} else {
		ctx->error_flag = 1;
	}

	ctx->reason = 0;
	ctx->interrupts = 0;

	/* do not disable device before master is idle */
	expire_ts = __hw_clock_source_read() + I2C_TSC_TIMEOUT;

	while (i2c_mmio_read(ctx->base, IC_STATUS) &
	       (1 << IC_STATUS_MASTER_ACTIVITY)) {

		if (__hw_clock_source_read() >= expire_ts) {
			ctx->error_flag = 1;
			break;
		}
	}

	i2c_mmio_write(ctx->base, IC_ENABLE, IC_ENABLE_DISABLE);

	return EC_SUCCESS;
}

static void i2c_interrupt_handler(struct i2c_context *ctx)
{
	/* check interrupts */
	ctx->interrupts = i2c_mmio_read(ctx->base, IC_INTR_STAT);
	ctx->reason = (uint16_t) i2c_mmio_read(ctx->base, IC_TX_ABRT_SOURCE);

	/* disable interrupts */
	i2c_intr_switch(ctx->base, DISABLE_INT);
	task_set_event(ctx->wait_task_id, EVENT_FLAG_I2C_TIMEOUT, 0);
}

static void i2c_isr_bus0(void)
{
	i2c_interrupt_handler(&i2c_ctxs[0]);
}
DECLARE_IRQ(ISH_I2C0_IRQ, i2c_isr_bus0);

static void i2c_isr_bus1(void)
{
	i2c_interrupt_handler(&i2c_ctxs[1]);
}
DECLARE_IRQ(ISH_I2C1_IRQ, i2c_isr_bus1);

static void i2c_isr_bus2(void)
{
	i2c_interrupt_handler(&i2c_ctxs[2]);
}
DECLARE_IRQ(ISH_I2C2_IRQ, i2c_isr_bus2);

static void i2c_init_hardware(struct i2c_context *ctx)
{
	uint32_t *base = ctx->base;

	/* disable interrupts */
	i2c_intr_switch(base, DISABLE_INT);
	i2c_mmio_write(base, IC_ENABLE, IC_ENABLE_DISABLE);
	i2c_mmio_write(base, IC_CON, (MASTER_MODE_VAL
				      | speed_val_arr[ctx->speed]
				      | IC_RESTART_EN_VAL
				      | IC_SLAVE_DISABLE_VAL));

	i2c_mmio_write(base, IC_FS_SPKLEN, spkln[bus_freq[ctx->bus]]);
	i2c_mmio_write(base, IC_HS_SPKLEN, spkln[bus_freq[ctx->bus]]);

	/* get RX_FIFO and TX_FIFO depth */
	ctx->max_rx_depth = i2c_read_byte(base, IC_COMP_PARAM_1,
					  RX_BUFFER_DEPTH_OFFSET) + 1;
	ctx->max_tx_depth = i2c_read_byte(base, IC_COMP_PARAM_1,
					  TX_BUFFER_DEPTH_OFFSET) + 1;
}

static void i2c_initial_board_config(struct i2c_context *ctx)
{
	uint8_t freq = bus_freq[ctx->bus];
	struct i2c_bus_info *bus_info = &board_config[ctx->bus];

	bus_info->std_speed.hcnt = default_hcnt_scl_100[freq];
	bus_info->std_speed.lcnt = default_lcnt_scl_100[freq];
	bus_info->fast_speed.hcnt = default_hcnt_scl_400[freq];
	bus_info->fast_speed.lcnt = default_lcnt_scl_400[freq];
	bus_info->high_speed.hcnt = default_hcnt_scl_hs[freq];
	bus_info->high_speed.lcnt = default_lcnt_scl_hs[freq];
}

static void i2c_init(void)
{
	int i;

	for (i = 0; i < ISH_I2C_PORT_COUNT; i++) {
		i2c_initial_board_config(&i2c_ctxs[i]);
		i2c_init_hardware(&i2c_ctxs[i]);
	}

	task_enable_irq(ISH_I2C0_IRQ);
	task_enable_irq(ISH_I2C1_IRQ);
	task_enable_irq(ISH_I2C2_IRQ);

	CPRINTS("Done i2c_init");
}
DECLARE_HOOK(HOOK_INIT, i2c_init, HOOK_PRIO_INIT_I2C);
