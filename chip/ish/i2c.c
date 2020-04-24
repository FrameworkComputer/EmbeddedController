/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* I2C port module for ISH */

#include "common.h"
#include "console.h"
#include "config_chip.h"
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

/*25MHz, 50MHz, 100MHz, 120MHz, 40MHz, 20MHz, 37MHz*/
static uint16_t default_hcnt_scl_100[] = {
	4000, 4420, 4920, 4400, 4000, 4000, 4300
};

static uint16_t default_lcnt_scl_100[] = {
	4720, 5180, 4990, 5333, 4700, 5200, 4950
};

static uint16_t default_hcnt_scl_400[] = {
	600, 820, 1120, 800, 600, 600, 450
};

static uint16_t default_lcnt_scl_400[] = {
	1320, 1380, 1300, 1550, 1300, 1200, 1250
};

static uint16_t default_hcnt_scl_1000[] = {
	260, 260, 260, 305, 260, 260, 260
};

static uint16_t default_lcnt_scl_1000[] = {
	500, 500, 500, 525, 500, 500, 500
};

static uint16_t default_hcnt_scl_hs[] = { 160, 300, 160, 166, 175, 150, 162 };
static uint16_t default_lcnt_scl_hs[] = { 320, 340, 320, 325, 325, 300, 297 };


#ifdef CHIP_VARIANT_ISH5P4
/* Change to I2C_FREQ_100 in real silicon platform */
static uint8_t bus_freq[ISH_I2C_PORT_COUNT] = {
	I2C_FREQ_100, I2C_FREQ_100, I2C_FREQ_100
};
#else
static uint8_t bus_freq[ISH_I2C_PORT_COUNT] = {
	I2C_FREQ_120, I2C_FREQ_120, I2C_FREQ_120
};
#endif

static struct i2c_context i2c_ctxs[ISH_I2C_PORT_COUNT] = {
	{
		.bus = 0,
		.base = (uint32_t *) ISH_I2C0_BASE,
		.speed = I2C_SPEED_400KHZ,
		.int_pin = ISH_I2C0_IRQ,
	},
	{
		.bus = 1,
		.base = (uint32_t *) ISH_I2C1_BASE,
		.speed = I2C_SPEED_400KHZ,
		.int_pin = ISH_I2C1_IRQ,
	},
	{
		.bus = 2,
		.base = (uint32_t *) ISH_I2C2_BASE,
		.speed = I2C_SPEED_400KHZ,
		.int_pin = ISH_I2C2_IRQ,
	},
};

static struct i2c_bus_info board_config[ISH_I2C_PORT_COUNT] = {
	{
		.bus_id = 0,
		.std_speed.sda_hold = DEFAULT_SDA_HOLD_STD,
		.fast_speed.sda_hold = DEFAULT_SDA_HOLD_FAST,
		.fast_plus_speed.sda_hold = DEFAULT_SDA_HOLD_FAST_PLUS,
		.high_speed.sda_hold = DEFAULT_SDA_HOLD_HIGH,
	},
	{
		.bus_id = 1,
		.std_speed.sda_hold = DEFAULT_SDA_HOLD_STD,
		.fast_speed.sda_hold = DEFAULT_SDA_HOLD_FAST,
		.fast_plus_speed.sda_hold = DEFAULT_SDA_HOLD_FAST_PLUS,
		.high_speed.sda_hold = DEFAULT_SDA_HOLD_HIGH,
	},
	{
		.bus_id = 2,
		.std_speed.sda_hold = DEFAULT_SDA_HOLD_STD,
		.fast_speed.sda_hold = DEFAULT_SDA_HOLD_FAST,
		.fast_plus_speed.sda_hold = DEFAULT_SDA_HOLD_FAST_PLUS,
		.high_speed.sda_hold = DEFAULT_SDA_HOLD_HIGH,
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
				 uint16_t slave_addr, uint8_t flags)
{
	uint32_t con_value;
	uint32_t *base = ctx->base;
	struct i2c_bus_info *bus_info = &board_config[ctx->bus];
	uint32_t clk_in_val = clk_in[bus_freq[ctx->bus]];

	/* disable interrupts */
	i2c_intr_switch(base, DISABLE_INT);

	i2c_mmio_write(base, IC_ENABLE, IC_ENABLE_DISABLE);
	i2c_mmio_write(base, IC_TAR, (slave_addr << IC_TAR_OFFSET) |
			TAR_SPECIAL_VAL | IC_10BITADDR_MASTER_VAL);

	/* set Clock SCL Count */
	switch (ctx->speed) {

	case I2C_SPEED_100KHZ:
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

	case I2C_SPEED_400KHZ:
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

	case I2C_SPEED_1MHZ:
		i2c_mmio_write(base, IC_FS_SCL_HCNT,
				NS_2_COUNTERS(bus_info->fast_plus_speed.hcnt,
					     clk_in_val));
		i2c_mmio_write(base, IC_FS_SCL_LCNT,
				NS_2_COUNTERS(bus_info->fast_plus_speed.lcnt,
					     clk_in_val));
		i2c_mmio_write(base, IC_SDA_HOLD,
				NS_2_COUNTERS(bus_info->fast_plus_speed.sda_hold,
					     clk_in_val));
		break;

	case I2C_SPEED_3M4HZ:
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

	if (flags != 0)
		con_value |= IC_RESTART_EN_VAL;
	else
		con_value &= ~IC_RESTART_EN_VAL;

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

		/* if Write ONLY and Last byte */
		if (*cur_index == total_len) {
			out |= DATA_CMD_STOP_VAL;
		}

		i2c_mmio_write(base, IC_DATA_CMD, out);
	}
}

static void i2c_write_read_commands(uint32_t *base, uint8_t len, int more_data,
					unsigned restart_flag)
{
	/* this routine just set RX FIFO's control bit(s),
	 * READ command or RESTART */
	int i;
	uint32_t data_cmd;

	for (i = 0; i < len; i++) {
		data_cmd = DATA_CMD_READ_VAL;

		if ((i == 0) && restart_flag)
			/* if restart for first byte */
			data_cmd |= DATA_CMD_RESTART_VAL;

		/* if last byte & less than FIFO size
		 * or only one byte to read */
		if (i == (len - 1) && !more_data)
			data_cmd |= DATA_CMD_STOP_VAL;

		i2c_mmio_write(base, IC_DATA_CMD, data_cmd);
	}
}

int chip_i2c_xfer(const int port, const uint16_t slave_addr_flags,
		  const uint8_t *out, int out_size,
		  uint8_t *in, int in_size, int flags)
{
	int i;
	ssize_t total_len;
	uint64_t expire_ts;
	struct i2c_context *ctx;
	ssize_t curr_index = 0;
	uint16_t addr = I2C_GET_ADDR(slave_addr_flags);
	int begin_indx;
	uint8_t repeat_start = 0;

	if (out_size == 0 && in_size == 0)
		return EC_SUCCESS;

	if (port < 0 || port >= ISH_I2C_PORT_COUNT)
		return EC_ERROR_INVAL;

	/* Check for reserved I2C addresses, pg. 74 in DW_apb_i2c.pdf
	 * Address cannot be any of the reserved address locations
	 */
	if (addr < I2C_FIRST_VALID_ADDR || addr > I2C_LAST_VALID_ADDR)
		return EC_ERROR_INVAL;

	/* assume that if both out_size and in_size are not zero,
	 * then, it is 'repeated Start' condition. */
	if (in_size != 0 && out_size != 0)
		repeat_start = 1;

	ctx = &i2c_ctxs[port];
	ctx->error_flag = 0;
	ctx->wait_task_id = task_get_current();

	total_len = in_size + out_size;

	i2c_init_transaction(ctx, addr, repeat_start);

	/* Write W data */
	if (out_size)
		i2c_write_buffer(ctx->base, out_size, out,
				&curr_index, total_len);

	/* Wait here until Tx is completed so that FIFO becomes empty.
	 * This is optimized for smaller Tx data size.
	 * If need to write big data ( > ISH_I2C_FIFO_SIZE ),
	 * it is better to use Tx FIFO threshold interrupt(as in Rx) for
	 * better CPU usuage.
	 * */
	expire_ts = __hw_clock_source_read() + I2C_TX_FLUSH_TIMEOUT_USEC;
	if (in_size > (ISH_I2C_FIFO_SIZE - out_size)) {

		while ((i2c_mmio_read(ctx->base, IC_STATUS) &
			BIT(IC_STATUS_TFE)) == 0) {

			if (__hw_clock_source_read() >= expire_ts) {
				ctx->error_flag = 1;
				break;
			}
			CPU_RELAX();
		}
	}

	begin_indx = 0;
	while (in_size) {
		int rd_size;  /* read size for on i2c transaction */

		/*
		 * check if in_size > ISH_I2C_FIFO_SIZE, then try to read
		 * FIFO_SIZE each time.
		 */
		if (in_size > ISH_I2C_FIFO_SIZE) {
			rd_size = ISH_I2C_FIFO_SIZE;
			in_size -= ISH_I2C_FIFO_SIZE;
		} else {
			rd_size = in_size;
			in_size = 0;
		}
		/* Set rx_threshold */
		i2c_mmio_write(ctx->base, IC_RX_TL, rd_size - 1);

		i2c_intr_switch(ctx->base, ENABLE_READ_INT);

		/*
		 * RESTART only once for entire i2c transaction.
		 * assume that if both out_size and in_size are not zero,
		 * then, it is 'repeated Start' condition.
		 * set R commands bit, start to read
		 */
		i2c_write_read_commands(ctx->base, rd_size, in_size,
				(begin_indx == 0) && (repeat_start != 0));


		/* need timeout in case no ACK from slave */
		task_wait_event_mask(TASK_EVENT_I2C_IDLE, 2*MSEC);

		if (ctx->interrupts & M_TX_ABRT) {
			ctx->error_flag = 1;
			break; /* when bus abort, no more reading !*/
		}

		/* read data */
		for (i = begin_indx; i < begin_indx + rd_size; i++)
			in[i] = i2c_read_byte(ctx->base,
					IC_DATA_CMD, 0);

		begin_indx += rd_size;
	} /* while (in_size) */

	ctx->reason = 0;
	ctx->interrupts = 0;

	/* do not disable device before master is idle */
	expire_ts = __hw_clock_source_read() + I2C_TSC_TIMEOUT;

	while ((i2c_mmio_read(ctx->base, IC_STATUS) &
		(BIT(IC_STATUS_MASTER_ACTIVITY) | BIT(IC_STATUS_TFE))) !=
	       BIT(IC_STATUS_TFE)) {

		if (__hw_clock_source_read() >= expire_ts) {
			ctx->error_flag = 1;
			break;
		}
	}

	i2c_intr_switch(ctx->base, DISABLE_INT);
	i2c_mmio_write(ctx->base, IC_ENABLE, IC_ENABLE_DISABLE);

	if (ctx->error_flag)
		return EC_ERROR_INVAL;

	return EC_SUCCESS;
}

static void i2c_interrupt_handler(struct i2c_context *ctx)
{
	uint32_t raw_intr;

	if (IS_ENABLED(INTR_DEBUG))
		raw_intr = 0x0000FFFF & i2c_mmio_read(ctx->base,
						      IC_RAW_INTR_STAT);

	/* check interrupts */
	ctx->interrupts = i2c_mmio_read(ctx->base, IC_INTR_STAT);
	ctx->reason = (uint16_t) i2c_mmio_read(ctx->base, IC_TX_ABRT_SOURCE);

	if (IS_ENABLED(INTR_DEBUG))
		CPRINTS("INTR_STAT = 0x%04x, TX_ABORT_SRC = 0x%04x, "
			"RAW_INTR_STAT = 0x%04x",
			ctx->interrupts, ctx->reason, raw_intr);

	/* disable interrupts */
	i2c_intr_switch(ctx->base, DISABLE_INT);
	task_set_event(ctx->wait_task_id, TASK_EVENT_I2C_IDLE, 0);
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

static void  i2c_config_speed(struct i2c_context *ctx, int kbps)
{

	if (kbps > 1000)
		ctx->speed = I2C_SPEED_3M4HZ;
	else if (kbps > 400)
		ctx->speed = I2C_SPEED_1MHZ;
	else if (kbps > 100)
		ctx->speed = I2C_SPEED_400KHZ;
	else
		ctx->speed = I2C_SPEED_100KHZ;

}

static void i2c_init_hardware(struct i2c_context *ctx)
{
	static const uint8_t speed_val_arr[] = {
		[I2C_SPEED_100KHZ] = STD_SPEED_VAL,
		[I2C_SPEED_400KHZ] = FAST_SPEED_VAL,
		[I2C_SPEED_1MHZ]   = FAST_SPEED_VAL,
		[I2C_SPEED_3M4HZ]  = HIGH_SPEED_VAL,
	};

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

	bus_info->fast_plus_speed.hcnt = default_hcnt_scl_1000[freq];
	bus_info->fast_plus_speed.lcnt = default_lcnt_scl_1000[freq];

	bus_info->high_speed.hcnt = default_hcnt_scl_hs[freq];
	bus_info->high_speed.lcnt = default_lcnt_scl_hs[freq];
}

void i2c_port_restore(void)
{
	for (int i = 0; i < i2c_ports_used; i++) {
		int port = i2c_ports[i].port;
		i2c_init_hardware(&i2c_ctxs[port]);
	}
}

void i2c_init(void)
{
	for (int i = 0; i < i2c_ports_used; i++) {
		int port = i2c_ports[i].port;
		i2c_initial_board_config(&i2c_ctxs[port]);
		/* Config speed from i2c_ports[] defined in board.c */
		i2c_config_speed(&i2c_ctxs[port], i2c_ports[i].kbps);
		i2c_init_hardware(&i2c_ctxs[port]);

		task_enable_irq((&i2c_ctxs[port])->int_pin);
	}

	CPRINTS("Done i2c_init");
}
