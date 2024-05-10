/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* I2C port module for MCHP MEC */

#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "i2c_chip.h"
#include "registers.h"
#include "task.h"
#include "tfdp_chip.h"
#include "timer.h"
#include "util.h"

#define CPUTS(outstr) cputs(CC_I2C, outstr)
#define CPRINTF(format, args...) cprintf(CC_I2C, format, ##args)
#define CPRINTS(format, args...) cprints(CC_I2C, format, ##args)

/*
 * MCHP I2C BAUD clock source is 16 MHz.
 */
#define I2C_CLOCK 16000000UL
#define MCHP_I2C_SUPPORTED_BUS_CLOCKS 6

/* SMBus Timing values for 1MHz Speed */
#define SPEED_1MHZ_BUS_CLOCK 0x0509ul
#define SPEED_1MHZ_DATA_TIMING 0x06060601ul
#define SPEED_1MHZ_DATA_TIMING_2 0x06ul
#define SPEED_1MHZ_IDLE_SCALING 0x01000050ul
#define SPEED_1MHZ_TIMEOUT_SCALING 0x149CC2C7ul
/* SMBus Timing values for 400kHz speed */
#define SPEED_400KHZ_BUS_CLOCK 0x0F17ul
#define SPEED_400KHZ_DATA_TIMING 0x040A0F01ul
#define SPEED_400KHZ_DATA_TIMING_2 0x0Aul
#define SPEED_400KHZ_IDLE_SCALING 0x01000050ul
#define SPEED_400KHZ_TIMEOUT_SCALING 0x149CC2C7ul
/* SMBus Timing values for 100kHz speed */
#define SPEED_100KHZ_BUS_CLOCK 0x4F4Ful
#define SPEED_100KHZ_DATA_TIMING 0x0C4D4306ul
#define SPEED_100KHZ_DATA_TIMING_2 0x4Dul
#define SPEED_100KHZ_IDLE_SCALING 0x01FC01EDul
#define SPEED_100KHZ_TIMEOUT_SCALING 0x4B9CC2C7ul
/* Bus clock dividers for 333, 80, and 40 kHz */
#define SPEED_333KHZ_BUS_CLOCK 0x0F1Ful
#define SPEED_80KHZ_BUS_CLOCK 0x6363ul
#define SPEED_40KHZ_BUS_CLOCK 0xC7C7ul

/* Status */
#define STS_NBB BIT(0) /* Bus busy */
#define STS_LAB BIT(1) /* Arbitration lost */
#define STS_LRB BIT(3) /* Last received bit */
#define STS_BER BIT(4) /* Bus error */
#define STS_PIN BIT(7) /* Pending interrupt */
/* Control */
#define CTRL_ACK BIT(0) /* Acknowledge */
#define CTRL_STO BIT(1) /* STOP */
#define CTRL_STA BIT(2) /* START */
#define CTRL_ENI BIT(3) /* Enable interrupt */
#define CTRL_ESO BIT(6) /* Enable serial output */
#define CTRL_PIN BIT(7) /* Pending interrupt not */
/* Completion */
#define COMP_DTEN BIT(2) /* enable device timeouts */
#define COMP_MCEN BIT(3) /* enable ctrl. cumulative timeouts */
#define COMP_SCEN BIT(4) /* enable periph. cumulative timeouts */
#define COMP_BIDEN BIT(5) /* enable Bus idle timeouts */
#define COMP_IDLE BIT(29) /* i2c bus is idle */
#define COMP_RW_BITS_MASK 0x3C /* R/W bits mask */
/* Configuration */
#define CFG_PORT_MASK (0x0F) /* port selection field */
#define CFG_TCEN BIT(4) /* Enable HW bus timeouts */
#define CFG_FEN BIT(8) /* enable input filtering */
#define CFG_RESET BIT(9) /* reset controller */
#define CFG_ENABLE BIT(10) /* enable controller */
#define CFG_GC_DIS BIT(14) /* disable general call address */
#define CFG_ENIDI BIT(29) /* Enable I2C idle interrupt */
/* Enable network layer controller done interrupt */
#define CFG_ENMI BIT(30)
/* Enable network layer peripheral done interrupt */
#define CFG_ENSI BIT(31)
/* Controller Command */
#define MCMD_MRUN BIT(0)
#define MCMD_MPROCEED BIT(1)
#define MCMD_START0 BIT(8)
#define MCMD_STARTN BIT(9)
#define MCMD_STOP BIT(10)
#define MCMD_READM BIT(12)
#define MCMD_WCNT_BITPOS (16)
#define MCMD_WCNT_MASK0 (0xFF)
#define MCMD_WCNT_MASK (0xFF << 16)
#define MCMD_RCNT_BITPOS (24)
#define MCMD_RCNT_MASK0 (0xFF)
#define MCMD_RCNT_MASK (0xFF << 24)

/* Maximum transfer of a SMBUS block transfer */
#define SMBUS_MAX_BLOCK_SIZE 32
/*
 * Amount of time to blocking wait for i2c bus to finish. After this
 * blocking timeout, if the bus is still not finished, then allow other
 * tasks to run.
 * Note: this is just long enough for a 400kHz bus to finish transmitting
 * one byte assuming the bus isn't being held.
 */
#define I2C_WAIT_BLOCKING_TIMEOUT_US 25

enum i2c_transaction_state {
	/* Stop condition was sent in previous transaction */
	I2C_TRANSACTION_STOPPED,
	/* Stop condition was not sent in previous transaction */
	I2C_TRANSACTION_OPEN,
};

/* I2C controller state data
 * NOTE: I2C_CONTROLLER_COUNT is defined at board level.
 */
static struct {
	/* Transaction timeout, or 0 to use default. */
	uint32_t timeout_us;
	/* Task waiting on port, or TASK_ID_INVALID if none. */
	/*
	 * MCHP Remove volatile.
	 * ISR only reads.
	 * Non-ISR only writes when interrupt is disabled.
	 */
	task_id_t task_waiting;
	enum i2c_transaction_state transaction_state;
	/* transaction context */
	int out_size;
	const uint8_t *outp;
	int in_size;
	uint8_t *inp;
	int xflags;
	uint32_t i2c_complete; /* ISR write */
	uint32_t flags;
	uint8_t port;
	uint8_t periph_addr_8bit;
	uint8_t ctrl;
	uint8_t hwsts;
	uint8_t hwsts2;
	uint8_t hwsts3; /* ISR write */
	uint8_t hwsts4;
	uint8_t lines;
} cdata[I2C_CONTROLLER_COUNT];

static const uint16_t i2c_ctrl_nvic_id[] = { MCHP_IRQ_I2C_0, MCHP_IRQ_I2C_1,
					     MCHP_IRQ_I2C_2, MCHP_IRQ_I2C_3,
#if defined(CHIP_FAMILY_MEC172X)
					     MCHP_IRQ_I2C_4
#elif defined(CHIP_FAMILY_MEC152X)
					     MCHP_IRQ_I2C_4, MCHP_IRQ_I2C_5,
					     MCHP_IRQ_I2C_6, MCHP_IRQ_I2C_7
#endif
};
BUILD_ASSERT(ARRAY_SIZE(i2c_ctrl_nvic_id) == MCHP_I2C_CTRL_MAX);

static const uint16_t i2c_controller_pcr[] = { MCHP_PCR_I2C0, MCHP_PCR_I2C1,
					       MCHP_PCR_I2C2, MCHP_PCR_I2C3,
#if defined(CHIP_FAMILY_MEC172X)
					       MCHP_PCR_I2C4
#elif defined(CHIP_FAMILY_MEC152X)
	MCHP_PCR_I2C4, MCHP_PCR_I2C5, MCHP_PCR_I2C6, MCHP_PCR_I2C7,
#endif
};
BUILD_ASSERT(ARRAY_SIZE(i2c_controller_pcr) == MCHP_I2C_CTRL_MAX);

static uintptr_t i2c_ctrl_base_addr[] = { MCHP_I2C0_BASE, MCHP_I2C1_BASE,
					  MCHP_I2C2_BASE, MCHP_I2C3_BASE,
#if defined(CHIP_FAMILY_MEC172X)
					  MCHP_I2C4_BASE
#elif defined(CHIP_FAMILY_MEC152X)
					  MCHP_I2C4_BASE,
					  /* NOTE: 5-7 do not implement network
					     layer hardware */
					  MCHP_I2C5_BASE, MCHP_I2C6_BASE,
					  MCHP_I2C7_BASE
#endif
};
BUILD_ASSERT(ARRAY_SIZE(i2c_ctrl_base_addr) == MCHP_I2C_CTRL_MAX);

static bool chip_i2c_is_controller_valid(int controller)
{
	if ((controller < 0) || (controller >= MCHP_I2C_CTRL_MAX))
		return false;
	return true;
}

static uintptr_t chip_i2c_ctrl_base(int controller)
{
	if (!chip_i2c_is_controller_valid(controller))
		return 0;

	return i2c_ctrl_base_addr[controller];
}

static uint32_t chip_i2c_ctrl_nvic_id(int controller)
{
	if (!chip_i2c_is_controller_valid(controller))
		return 0;

	return (uint32_t)i2c_ctrl_nvic_id[controller];
}

static void i2c_ctrl_slp_en(int controller, int sleep_en)
{
	if (!chip_i2c_is_controller_valid(controller))
		return;
	if (sleep_en)
		MCHP_PCR_SLP_EN_DEV(i2c_controller_pcr[controller]);
	else
		MCHP_PCR_SLP_DIS_DEV(i2c_controller_pcr[controller]);
}

uint32_t chip_i2c_get_ctx_flags(int port)
{
	int controller = i2c_port_to_controller(port);

	if (!chip_i2c_is_controller_valid(controller))
		return 0;
	return cdata[controller].flags;
}

/*
 * MCHP I2C controller tuned bus clock values.
 * MCHP I2C_SMB_Controller_3.6.pdf Table 6-3
 */
struct i2c_bus_clk {
	int freq_khz;
	int bus_clk;
};

const struct i2c_bus_clk i2c_freq_tbl[] = {
	{ 40, SPEED_40KHZ_BUS_CLOCK },	 { 80, SPEED_80KHZ_BUS_CLOCK },
	{ 100, SPEED_100KHZ_BUS_CLOCK }, { 333, SPEED_333KHZ_BUS_CLOCK },
	{ 400, SPEED_400KHZ_BUS_CLOCK }, { 1000, SPEED_1MHZ_BUS_CLOCK },
};
BUILD_ASSERT(ARRAY_SIZE(i2c_freq_tbl) == MCHP_I2C_SUPPORTED_BUS_CLOCKS);

/* I2C controller assignment to a port */
static int i2c_p2c[MCHP_I2C_PORT_COUNT];

static int get_closest(int lesser, int greater, int target)
{
	if (target - i2c_freq_tbl[lesser].freq_khz >=
	    i2c_freq_tbl[greater].freq_khz - target)
		return greater;
	else
		return lesser;
}

/*
 * Return index in i2c_freq_tbl of supported frequencies
 * closest to requested frequency.
 */
static const struct i2c_bus_clk *get_supported_speed_idx(int req_kbps)
{
	int i, limit, m, imax;

	if (req_kbps <= i2c_freq_tbl[0].freq_khz)
		return &i2c_freq_tbl[0];

	imax = ARRAY_SIZE(i2c_freq_tbl);
	if (req_kbps >= i2c_freq_tbl[imax - 1].freq_khz)
		return &i2c_freq_tbl[imax - 1];

	/* we only get here if ARRAY_SIZE(...) > 1
	 * and req_kbps is in range.
	 */
	i = 0;
	limit = imax;
	while (i < limit) {
		m = (i + limit) / 2;
		if (i2c_freq_tbl[m].freq_khz == req_kbps)
			break;

		if (req_kbps < i2c_freq_tbl[m].freq_khz) {
			if (m > 0 && req_kbps > i2c_freq_tbl[m - 1].freq_khz) {
				m = get_closest(m - 1, m, req_kbps);
				break;
			}
			limit = m;
		} else {
			if (m < imax - 1 &&
			    req_kbps < i2c_freq_tbl[m + 1].freq_khz) {
				m = get_closest(m, m + 1, req_kbps);
				break;
			}
			i = m + 1;
		}
	}

	return &i2c_freq_tbl[m];
}

/*
 * Refer to NXP UM10204 for minimum timing requirement of T_Low and T_High.
 * http://www.nxp.com/documents/user_manual/UM10204.pdf
 * I2C spec. timing value are used in recommended registers values
 * in MCHP I2C_SMB_Controller_3.6.pdf
 * Restrict frequencies to those in the above MCHP spec.
 * 40, 80, 100, 333, 400, and 1000 kHz.
 */
static void configure_controller_speed(int controller, int kbps)
{
	const struct i2c_bus_clk *p;
	uintptr_t raddr;

	raddr = chip_i2c_ctrl_base(controller);

	p = get_supported_speed_idx(kbps);
	MCHP_I2C_BUS_CLK(raddr) = p->bus_clk;

	if (p->freq_khz > 400) { /* Fast mode plus */
		MCHP_I2C_DATA_TIM(raddr) = SPEED_1MHZ_DATA_TIMING;
		MCHP_I2C_DATA_TIM_2(raddr) = SPEED_1MHZ_DATA_TIMING_2;
		MCHP_I2C_IDLE_SCALE(raddr) = SPEED_1MHZ_IDLE_SCALING;
		MCHP_I2C_TOUT_SCALE(raddr) = SPEED_1MHZ_TIMEOUT_SCALING;
	} else if (p->freq_khz > 100) { /* Fast mode */
		MCHP_I2C_DATA_TIM(raddr) = SPEED_400KHZ_DATA_TIMING;
		MCHP_I2C_DATA_TIM_2(raddr) = SPEED_400KHZ_DATA_TIMING_2;
		MCHP_I2C_IDLE_SCALE(raddr) = SPEED_400KHZ_IDLE_SCALING;
		MCHP_I2C_TOUT_SCALE(raddr) = SPEED_400KHZ_TIMEOUT_SCALING;
	} else { /* Standard mode */
		MCHP_I2C_DATA_TIM(raddr) = SPEED_100KHZ_DATA_TIMING;
		MCHP_I2C_DATA_TIM_2(raddr) = SPEED_100KHZ_DATA_TIMING_2;
		MCHP_I2C_IDLE_SCALE(raddr) = SPEED_100KHZ_IDLE_SCALING;
		MCHP_I2C_TOUT_SCALE(raddr) = SPEED_100KHZ_TIMEOUT_SCALING;
	}
}

/*
 * NOTE: direct mode interrupts do not need GIRQn bit
 * set in aggregator block enable register.
 */
static void enable_controller_irq(int controller)
{
	uint32_t nvic_id = chip_i2c_ctrl_nvic_id(controller);

	MCHP_INT_ENABLE(MCHP_I2C_GIRQ) = MCHP_I2C_GIRQ_BIT(controller);
	task_enable_irq(nvic_id);
}

static void disable_controller_irq(int controller)
{
	uint32_t nvic_id = chip_i2c_ctrl_nvic_id(controller);

	MCHP_INT_DISABLE(MCHP_I2C_GIRQ) = MCHP_I2C_GIRQ_BIT(controller);
	/* read back into read-only reg. to insure disable takes effect */
	MCHP_INT_BLK_IRQ = MCHP_INT_DISABLE(MCHP_I2C_GIRQ);
	task_disable_irq(nvic_id);
	task_clear_pending_irq(nvic_id);
}

/*
 * Do NOT enable controller's IDLE interrupt in the configuration
 * register. IDLE is meant for multi-controller and controller acting
 * as a peripheral.
 */
static void configure_controller(int controller, int port, int kbps)
{
	uintptr_t raddr = chip_i2c_ctrl_base(controller);

	if (raddr == 0)
		return;

	disable_controller_irq(controller);
	MCHP_INT_SOURCE(MCHP_I2C_GIRQ) = MCHP_I2C_GIRQ_BIT(controller);

	/* set to default except for port select field b[3:0] */
	MCHP_I2C_CONFIG(raddr) = (uint32_t)(port & 0xf);
	MCHP_I2C_CTRL(raddr) = CTRL_PIN;

	/* Set both controller peripheral addresses to 0 the
	 * general call address. We disable general call
	 * below.
	 */
	MCHP_I2C_OWN_ADDR(raddr) = 0;

	configure_controller_speed(controller, kbps);

	/* Controller timings done, clear RO status, enable
	 * output, and ACK generation.
	 */
	MCHP_I2C_CTRL(raddr) = CTRL_PIN | CTRL_ESO | CTRL_ACK;

	/* filter enable, disable General Call */
	MCHP_I2C_CONFIG(raddr) |= CFG_FEN + CFG_GC_DIS;
	/* enable controller */
	MCHP_I2C_CONFIG(raddr) |= CFG_ENABLE;
}

static void reset_controller(int controller)
{
	int i;
	uintptr_t raddr;

	raddr = chip_i2c_ctrl_base(controller);
	if (raddr == 0)
		return;

	/* Reset asserted for at least one AHB clock */
	MCHP_I2C_CONFIG(raddr) |= BIT(9);
	MCHP_EC_ID_RO = 0;
	MCHP_I2C_CONFIG(raddr) &= ~BIT(9);

	for (i = 0; i < i2c_ports_used; ++i)
		if (controller == i2c_port_to_controller(i2c_ports[i].port)) {
			configure_controller(controller, i2c_ports[i].port,
					     i2c_ports[i].kbps);
			cdata[controller].transaction_state =
				I2C_TRANSACTION_STOPPED;
			break;
		}
}

/*
 * !!! WARNING !!!
 * We have observed task_wait_event_mask() returning 0 if the I2C
 * controller IDLE interrupt is enabled. We believe it is due to the ISR
 * post multiple events too quickly but don't have absolute proof.
 */
static int wait_for_interrupt(int controller, int timeout)
{
	int event;

	if (timeout <= 0)
		return EC_ERROR_TIMEOUT;

	cdata[controller].task_waiting = task_get_current();
	enable_controller_irq(controller);

	/* Wait until I2C interrupt or timeout. */
	event = task_wait_event_mask(TASK_EVENT_I2C_IDLE, timeout);

	disable_controller_irq(controller);
	cdata[controller].task_waiting = TASK_ID_INVALID;

	return (event & TASK_EVENT_TIMER) ? EC_ERROR_TIMEOUT : EC_SUCCESS;
}

static int wait_idle(int controller)
{
	uintptr_t raddr = chip_i2c_ctrl_base(controller);
	uint64_t block_timeout = get_time().val + I2C_WAIT_BLOCKING_TIMEOUT_US;
	uint64_t task_timeout = block_timeout + cdata[controller].timeout_us;
	int rv = 0;
	uint8_t sts = MCHP_I2C_STATUS(raddr);

	while (!(sts & STS_NBB)) {
		if (rv)
			return rv;
		if (get_time().val > block_timeout)
			rv = wait_for_interrupt(controller,
						task_timeout - get_time().val);
		sts = MCHP_I2C_STATUS(raddr);
	}

	if (sts & (STS_BER | STS_LAB))
		return EC_ERROR_UNKNOWN;
	return EC_SUCCESS;
}

/*
 * Return EC_SUCCESS on ACK of byte else EC_ERROR_UNKNOWN.
 * Record I2C.Status in cdata[controller] structure.
 * Byte transmit finished with no I2C bus error or lost arbitration.
 *	PIN -> 0. LRB bit contains peripheral ACK/NACK bit.
 *	Peripheral ACK:  I2C.Status == 0x00
 *	Peripheral NACK: I2C.Status == 0x08
 * Byte transmit finished with I2C bus errors or lost arbitration.
 *	PIN -> 0 and BER and/or LAB set.
 *
 * Byte receive finished with no I2C bus errors or lost arbitration.
 *	PIN -> 0. LRB=0/1 based on ACK bit in I2C.Control.
 *	Controller receiver must NACK last byte it wants to receive.
 *	How do we handle this if we don't know direction of transfer?
 *	I2C.Control is write-only so we can't see Controller's ACK control
 *	bit.
 */
static int wait_byte_done(int controller, uint8_t mask, uint8_t expected)
{
	uint64_t block_timeout;
	uint64_t task_timeout;
	uintptr_t raddr;
	int rv;
	uint8_t sts;

	rv = 0;
	raddr = chip_i2c_ctrl_base(controller);
	block_timeout = get_time().val + I2C_WAIT_BLOCKING_TIMEOUT_US;
	task_timeout = block_timeout + cdata[controller].timeout_us;
	sts = MCHP_I2C_STATUS(raddr);
	cdata[controller].hwsts = sts;
	while (sts & STS_PIN) {
		if (rv)
			return rv;
		if (get_time().val > block_timeout) {
			rv = wait_for_interrupt(controller,
						task_timeout - get_time().val);
		}
		sts = MCHP_I2C_STATUS(raddr);
		cdata[controller].hwsts = sts;
	}

	rv = EC_SUCCESS;
	if ((sts & mask) != expected)
		rv = EC_ERROR_UNKNOWN;
	return rv;
}

/*
 * Select port on controller. If controller configured
 * for port do nothing.
 * Switch port by reset and reconfigure to handle cases where
 * the peripheral on current port is driving line(s) low.
 * NOTE: I2C hardware reset only requires one AHB clock, back to back
 * writes is OK but we added an extra write as insurance.
 */
static void select_port(int port, int controller)
{
	uint32_t port_sel;
	uintptr_t raddr;

	raddr = chip_i2c_ctrl_base(controller);
	port_sel = (uint32_t)(port & 0x0f);
	if ((MCHP_I2C_CONFIG(raddr) & 0x0f) == port_sel)
		return;

	MCHP_I2C_CONFIG(raddr) |= BIT(9);
	MCHP_EC_ID_RO = 0; /* extra write to read-only as delay */
	MCHP_I2C_CONFIG(raddr) &= ~BIT(9);
	configure_controller(controller, port_sel, i2c_ports[port].kbps);
}

/*
 * Use safe method (reading GPIO.Control PAD input bit)
 * to obtain SCL line state in bit[0] and SDA line state in bit[1].
 * NOTE: I2C controller bit-bang register is not safe. Using
 * bit-bang requires timeouts be disabled and the controller in an
 * idle state. Switching controller to bit-bang mode when the controller
 * is not idle will cause problems.
 */
static uint32_t get_line_level(int port)
{
	uint32_t lines;

	lines = i2c_raw_get_scl(port) & 0x01;
	lines |= (i2c_raw_get_sda(port) & 0x01) << 1;
	return lines;
}

/*
 * Check if I2C port connected to controller has bus error or
 * other issues such as stuck clock/data lines.
 */
static int i2c_check_recover(int port, int controller)
{
	uintptr_t raddr;
	uint32_t lines;
	uint8_t reg;

	raddr = chip_i2c_ctrl_base(controller);
	lines = get_line_level(port);
	reg = MCHP_I2C_STATUS(raddr);

	if ((((reg & (STS_BER | STS_LAB)) || !(reg & STS_NBB)) ||
	     (lines != I2C_LINE_IDLE))) {
		cdata[controller].flags |= (1ul << 16);
		CPRINTS("I2C%d port%d recov status 0x%02x, SDA:SCL=0x%0x",
			controller, port, reg, lines);
		/* Attempt to unwedge the port. */
		if (lines != I2C_LINE_IDLE)
			if (i2c_unwedge(port))
				return EC_ERROR_UNKNOWN;

		/* Bus error, bus busy, or arbitration lost. Try reset. */
		reset_controller(controller);
		select_port(port, controller);
		/*
		 * We don't know what edges the peripheral saw, so sleep long
		 * enough that the peripheral will see the new start condition
		 * below.
		 */
		crec_usleep(1000);
		reg = MCHP_I2C_STATUS(raddr);
		lines = get_line_level(port);
		if ((reg & (STS_BER | STS_LAB)) || !(reg & STS_NBB) ||
		    (lines != I2C_LINE_IDLE))
			return EC_ERROR_UNKNOWN;
	}
	return EC_SUCCESS;
}

static inline void push_in_buf(uint8_t **in, uint8_t val, int skip)
{
	if (!skip) {
		**in = val;
		(*in)++;
	}
}

/*
 * I2C Controller transmit
 * Caller has filled in cdata[ctrl] parameters
 */
static int i2c_mtx(int ctrl)
{
	uintptr_t raddr;
	int i, rv;

	raddr = chip_i2c_ctrl_base(ctrl);
	rv = EC_SUCCESS;
	cdata[ctrl].flags |= (1ul << 1);
	if (cdata[ctrl].xflags & I2C_XFER_START) {
		cdata[ctrl].flags |= (1ul << 2);
		MCHP_I2C_DATA(raddr) = cdata[ctrl].periph_addr_8bit;
		/* Clock out the peripheral address, sending START bit */
		MCHP_I2C_CTRL(raddr) = CTRL_PIN | CTRL_ESO | CTRL_ENI |
				       CTRL_ACK | CTRL_STA;
		cdata[ctrl].transaction_state = I2C_TRANSACTION_OPEN;
	}

	for (i = 0; i < cdata[ctrl].out_size; ++i) {
		rv = wait_byte_done(ctrl, 0xff, 0x00);
		if (rv) {
			cdata[ctrl].flags |= (1ul << 17);
			MCHP_I2C_CTRL(ctrl) = CTRL_PIN | CTRL_ESO | CTRL_ENI |
					      CTRL_STO | CTRL_ACK;
			return rv;
		}
		cdata[ctrl].flags |= (1ul << 15);
		MCHP_I2C_DATA(raddr) = cdata[ctrl].outp[i];
	}

	rv = wait_byte_done(ctrl, 0xff, 0x00);
	if (rv) {
		cdata[ctrl].flags |= (1ul << 18);
		MCHP_I2C_CTRL(raddr) = CTRL_PIN | CTRL_ESO | CTRL_ENI |
				       CTRL_STO | CTRL_ACK;
		return rv;
	}

	/*
	 * Send STOP bit if the stop flag is on, and caller
	 * doesn't expect to receive data.
	 */
	if ((cdata[ctrl].xflags & I2C_XFER_STOP) &&
	    (cdata[ctrl].in_size == 0)) {
		cdata[ctrl].flags |= (1ul << 3);
		MCHP_I2C_CTRL(raddr) = CTRL_PIN | CTRL_ESO | CTRL_STO |
				       CTRL_ACK;
		cdata[ctrl].transaction_state = I2C_TRANSACTION_STOPPED;
	}
	return rv;
}

/*
 * I2C Controller-Receive helper routine for sending START or
 * Repeated-START.
 * This routine should only be called if a (Repeated-)START
 * is required.
 * If I2C controller is Idle or Stopped
 *   Send START by:
 *	Write read address to I2C.Data
 *	Write PIN=ESO=STA=ACK=1, STO=0 to I2C.Ctrl. This
 *	will trigger controller to output 8-bits of data.
 * Else if I2C controller is Open (previous START sent)
 *   Send Repeated-START by:
 *	Write ESO=STA=ACK=1, PIN=STO=0 to I2C.Ctrl. Controller
 *	will generate START but not transmit data.
 *	Write read address to I2C.Data. Controller will transmit
 *	8-bits of data
 * NOTE: Controller clocks in address on SDA as its transmitting.
 * Therefore 1-byte RX-FIFO will contain address plus R/nW bit.
 * Controller will wait for peripheral to release SCL before transmitting
 * 9th clock and latching (N)ACK on SDA.
 * Spin on I2C.Status PIN -> 0. Enable I2C interrupt if spin time
 * exceeds threshold. If a timeout occurs generate STOP and return
 * an error.
 *
 * Because I2C generates clocks for next byte when reading I2C.Data
 * register we must prepare control logic.
 * If the caller requests STOP and read length is 1 then set
 * clear ACK bit in I2C.Ctrl. Set ESO=ENI=1, PIN=STA=STO=ACK=0
 * in I2C.Ctrl. Controller must NACK last byte.
 */
static int i2c_mrx_start(int ctrl)
{
	uintptr_t raddr;
	int rv;
	uint8_t u8;

	raddr = chip_i2c_ctrl_base(ctrl);

	cdata[ctrl].flags |= (1ul << 4);
	u8 = CTRL_ESO | CTRL_ENI | CTRL_STA | CTRL_ACK;
	if (cdata[ctrl].transaction_state == I2C_TRANSACTION_OPEN) {
		cdata[ctrl].flags |= (1ul << 5);
		/* Repeated-START then address */
		MCHP_I2C_CTRL(raddr) = u8;
	}
	MCHP_I2C_DATA(raddr) = cdata[ctrl].periph_addr_8bit | 0x01;
	if (cdata[ctrl].transaction_state == I2C_TRANSACTION_STOPPED) {
		cdata[ctrl].flags |= (1ul << 6);
		/* address then START */
		MCHP_I2C_CTRL(raddr) = u8 | CTRL_PIN;
	}
	cdata[ctrl].transaction_state = I2C_TRANSACTION_OPEN;
	/* Controller generates START, transmits data(address) capturing
	 * 9-bits from SDA (8-bit address + (N)Ack bit).
	 * We leave captured address in I2C.Data register.
	 * Controller receive data read routine assumes data is pending
	 * in I2C.Data
	 */
	cdata[ctrl].flags |= (1ul << 7);
	rv = wait_byte_done(ctrl, 0xff, 0x00);
	if (rv) {
		cdata[ctrl].flags |= (1ul << 19);
		MCHP_I2C_CTRL(raddr) = CTRL_PIN | CTRL_ESO | CTRL_STO |
				       CTRL_ACK;
		return rv;
	}
	/* if STOP requested and last 1 or 2 bytes prepare controller
	 * to NACK last byte. Do this before read of extra data so
	 * controller is setup to NACK last byte.
	 */
	cdata[ctrl].flags |= (1ul << 8);
	if (cdata[ctrl].xflags & I2C_XFER_STOP && (cdata[ctrl].in_size < 2)) {
		cdata[ctrl].flags |= (1ul << 9);
		MCHP_I2C_CTRL(raddr) = CTRL_ESO | CTRL_ENI;
	}
	/*
	 * Read & discard peripheral address.
	 * Generates clocks for next data
	 */
	cdata[ctrl].flags |= (1ul << 10);
	u8 = MCHP_I2C_DATA(raddr);
	return rv;
}
/*
 * I2C Controller-Receive data read helper.
 * Assumes I2C is in use, (Rpt-)START was previously sent.
 * Reading I2C.Data generates clocks for the next byte. If caller
 * requests STOP then we must clear I2C.Ctrl ACK before reading
 * second to last byte from RX-FIFO data register. Before reading
 * the last byte we must set I2C.Ctrl to generate a stop after
 * the read from RX-FIFO register.
 * NOTE: I2C.Status.LRB only records the (N)ACK bit in controller
 * transmit mode, not in controller receive mode.
 * NOTE2: Do not set ENI bit in I2C.Ctrl for STOP generation.
 */
static int i2c_mrx_data(int ctrl)
{
	uint32_t nrx = (uint32_t)cdata[ctrl].in_size;
	uint32_t stop = (uint32_t)cdata[ctrl].xflags & I2C_XFER_STOP;
	uint8_t *pdest = cdata[ctrl].inp;
	int rv;
	uintptr_t raddr;

	raddr = chip_i2c_ctrl_base(ctrl);

	cdata[ctrl].flags |= (1ul << 11);
	while (nrx) {
		rv = wait_byte_done(ctrl, 0xff, 0x00);
		if (rv) {
			cdata[ctrl].flags |= (1ul << 20);
			MCHP_I2C_CTRL(raddr) = CTRL_PIN | CTRL_ESO | CTRL_STO |
					       CTRL_ACK;
			return rv;
		}
		if (stop) {
			if (nrx == 2) {
				cdata[ctrl].flags |= (1ul << 12);
				MCHP_I2C_CTRL(raddr) = CTRL_ESO | CTRL_ENI;
			} else if (nrx == 1) {
				cdata[ctrl].flags |= (1ul << 13);
				MCHP_I2C_CTRL(raddr) = CTRL_PIN | CTRL_ESO |
						       CTRL_STO | CTRL_ACK;
			}
		}
		*pdest++ = MCHP_I2C_DATA(raddr);
		nrx--;
	}
	cdata[ctrl].flags |= (1ul << 14);
	return EC_SUCCESS;
}

/*
 * Called from common I2C
 */
int chip_i2c_xfer(int port, uint16_t periph_addr_flags, const uint8_t *out,
		  int out_size, uint8_t *in, int in_size, int flags)
{
	int ctrl;
	int ret_done;
	uintptr_t raddr;

	if (out_size == 0 && in_size == 0)
		return EC_SUCCESS;

	ctrl = i2c_port_to_controller(port);
	if (ctrl < 0)
		return EC_ERROR_INVAL;

	raddr = chip_i2c_ctrl_base(ctrl);
	if (raddr == 0)
		return EC_ERROR_INVAL;

	cdata[ctrl].flags = (1ul << 0);
	disable_controller_irq(ctrl);
	select_port(port, ctrl);

	/* store transfer context */
	cdata[ctrl].i2c_complete = 0;
	cdata[ctrl].hwsts = 0;
	cdata[ctrl].hwsts2 = 0;
	cdata[ctrl].hwsts3 = 0;
	cdata[ctrl].hwsts4 = 0;
	cdata[ctrl].port = port & 0xff;
	cdata[ctrl].periph_addr_8bit = I2C_STRIP_FLAGS(periph_addr_flags) << 1;
	cdata[ctrl].out_size = out_size;
	cdata[ctrl].outp = out;
	cdata[ctrl].in_size = in_size;
	cdata[ctrl].inp = in;
	cdata[ctrl].xflags = flags;

	if ((flags & I2C_XFER_START) &&
	    cdata[ctrl].transaction_state == I2C_TRANSACTION_STOPPED) {
		wait_idle(ctrl);
		ret_done = i2c_check_recover(port, ctrl);
		if (ret_done)
			goto err_chip_i2c_xfer;
	}

	ret_done = EC_SUCCESS;
	if (out_size) {
		ret_done = i2c_mtx(ctrl);
		if (ret_done)
			goto err_chip_i2c_xfer;
	}

	if (in_size) {
		if (cdata[ctrl].xflags & I2C_XFER_START) {
			ret_done = i2c_mrx_start(ctrl);
			if (ret_done)
				goto err_chip_i2c_xfer;
		}
		ret_done = i2c_mrx_data(ctrl);
		if (ret_done)
			goto err_chip_i2c_xfer;
	}

	cdata[ctrl].flags |= (1ul << 15);
	/* MCHP wait for STOP to complete */
	if (cdata[ctrl].xflags & I2C_XFER_STOP)
		wait_idle(ctrl);

	/* Check for error conditions */
	if (MCHP_I2C_STATUS(raddr) & (STS_LAB | STS_BER)) {
		cdata[ctrl].flags |= (1ul << 21);
		goto err_chip_i2c_xfer;
	}
	cdata[ctrl].flags |= (1ul << 14);
	return EC_SUCCESS;

err_chip_i2c_xfer:
	cdata[ctrl].flags |= (1ul << 22);
	cdata[ctrl].hwsts2 = MCHP_I2C_STATUS(raddr); /* record status */
	/* NOTE: writing I2C.Ctrl.PIN=1 will clear all bits
	 * except NBB in I2C.Status
	 */
	MCHP_I2C_CTRL(raddr) = CTRL_PIN | CTRL_ESO | CTRL_STO | CTRL_ACK;
	cdata[ctrl].transaction_state = I2C_TRANSACTION_STOPPED;
	/* record status after STOP */
	cdata[ctrl].hwsts4 = MCHP_I2C_STATUS(raddr);

	/* record line levels.
	 * Note line levels may reflect STOP condition
	 */
	cdata[ctrl].lines = (uint8_t)get_line_level(cdata[ctrl].port);
	if (cdata[ctrl].hwsts2 & STS_BER) {
		cdata[ctrl].flags |= (1ul << 23);
		reset_controller(ctrl);
	}
	return EC_ERROR_UNKNOWN;
}
/*
 * A safe method of reading port's SCL pin level.
 */
int i2c_raw_get_scl(int port)
{
	enum gpio_signal g;

	/* If no SCL pin defined for this port,
	 * then return 1 to appear idle.
	 */
	if (get_scl_from_i2c_port(port, &g) != EC_SUCCESS)
		return 1;
	return gpio_get_level(g);
}

/*
 * A safe method of reading port's SDA pin level.
 */
int i2c_raw_get_sda(int port)
{
	enum gpio_signal g;

	/* If no SDA pin defined for this port,
	 * then return 1 to appear idle.
	 */
	if (get_sda_from_i2c_port(port, &g) != EC_SUCCESS)
		return 1;
	return gpio_get_level(g);
}

/*
 * Caller is responsible for locking the port.
 */
int i2c_get_line_levels(int port)
{
	int rv, controller;

	controller = i2c_port_to_controller(port);
	if (controller < 0)
		return 0x03; /* No controller, return high line levels */

	select_port(port, controller);
	rv = get_line_level(port);
	return rv;
}

/*
 * this function returns the controller for I2C
 * return mod of MCHP_I2C_CTRL_MAX
 */
__overridable int board_i2c_p2c(int port)
{
	if (port < 0 || port >= I2C_PORT_COUNT)
		return -1;
	return i2c_p2c[port];
}

/*
 * I2C port must be a zero based number.
 * MCHP I2C can map any port to any of the 4 controllers.
 * Call board level function as board designs may choose
 * to wire up and group ports differently.
 */
int i2c_port_to_controller(int port)
{
	return board_i2c_p2c(port);
}

void i2c_set_timeout(int port, uint32_t timeout)
{
	/* Parameter is port, but timeout is stored by-controller. */
	cdata[i2c_port_to_controller(port)].timeout_us =
		timeout ? timeout : I2C_TIMEOUT_DEFAULT_US;
}

/*
 * Initialize I2C controllers specified by the board configuration.
 * If multiple ports are mapped to the same controller choose the
 * lowest speed.
 */
void i2c_init(void)
{
	int i, controller, kbps;
	int controller_kbps[MCHP_I2C_CTRL_MAX];
	const struct i2c_bus_clk *pbc;

	for (i = 0; i < MCHP_I2C_CTRL_MAX; i++)
		controller_kbps[i] = 0;

	/* Configure GPIOs */
	gpio_config_module(MODULE_I2C, 1);

	memset(cdata, 0, sizeof(cdata));

	for (i = 0; i < i2c_ports_used; ++i) {
		/* Assign I2C controller to I2C port */
		i2c_p2c[i2c_ports[i].port] = i % MCHP_I2C_CTRL_MAX;

		controller = i2c_port_to_controller(i2c_ports[i].port);
		kbps = i2c_ports[i].kbps;

		/* Clear PCR sleep enable for controller */
		i2c_ctrl_slp_en(controller, 0);

		if (controller_kbps[controller] &&
		    (controller_kbps[controller] != kbps)) {
			CPRINTF("I2C[%d] init speed conflict: %d != %d\n",
				controller, kbps, controller_kbps[controller]);
			kbps = MIN(kbps, controller_kbps[controller]);
		}

		/* controller speed hardware limits */
		pbc = get_supported_speed_idx(kbps);
		if (pbc->freq_khz != kbps)
			CPRINTF("I2C[%d] init requested speed %d"
				" using closest supported speed %d\n",
				controller, kbps, pbc->freq_khz);

		controller_kbps[controller] = pbc->freq_khz;
		configure_controller(controller, i2c_ports[i].port,
				     controller_kbps[controller]);
		cdata[controller].task_waiting = TASK_ID_INVALID;
		cdata[controller].transaction_state = I2C_TRANSACTION_STOPPED;
		/* Use default timeout. */
		i2c_set_timeout(i2c_ports[i].port, 0);
	}
}

/*
 * Handle I2C interrupts.
 * I2C controller is configured to fire interrupts on
 * anything causing PIN 1->0 and I2C IDLE (NBB -> 1).
 * NVIC interrupt disable must clear NVIC pending bit.
 */
static void handle_interrupt(int controller)
{
	uint32_t r;
	int id = cdata[controller].task_waiting;
	uintptr_t raddr = chip_i2c_ctrl_base(controller);

	/*
	 * Write to control register interferes with I2C transaction.
	 * Instead, let's disable IRQ from the core until the next time
	 * we want to wait for STS_PIN/STS_NBB.
	 */
	disable_controller_irq(controller);
	cdata[controller].hwsts3 = MCHP_I2C_STATUS(raddr);
	/* Clear all interrupt status */
	r = MCHP_I2C_COMPLETE(raddr);
	MCHP_I2C_COMPLETE(raddr) = r;
	cdata[controller].i2c_complete = r;
	MCHP_INT_SOURCE(MCHP_I2C_GIRQ) = MCHP_I2C_GIRQ_BIT(controller);

	/* Wake up the task which was waiting on the I2C interrupt, if any. */
	if (id != TASK_ID_INVALID)
		task_set_event(id, TASK_EVENT_I2C_IDLE);
}

static void i2c0_interrupt(void)
{
	handle_interrupt(0);
}
static void i2c1_interrupt(void)
{
	handle_interrupt(1);
}
static void i2c2_interrupt(void)
{
	handle_interrupt(2);
}
static void i2c3_interrupt(void)
{
	handle_interrupt(3);
}
#if defined(CHIP_FAMILY_MEC172X)
static void i2c4_interrupt(void)
{
	handle_interrupt(4);
}
#elif defined(CHIP_FAMILY_MEC152X)
static void i2c4_interrupt(void)
{
	handle_interrupt(4);
}
static void i2c5_interrupt(void)
{
	handle_interrupt(5);
}
static void i2c6_interrupt(void)
{
	handle_interrupt(6);
}
static void i2c7_interrupt(void)
{
	handle_interrupt(7);
}
#endif

DECLARE_IRQ(MCHP_IRQ_I2C_0, i2c0_interrupt, 2);
DECLARE_IRQ(MCHP_IRQ_I2C_1, i2c1_interrupt, 2);
DECLARE_IRQ(MCHP_IRQ_I2C_2, i2c2_interrupt, 2);
DECLARE_IRQ(MCHP_IRQ_I2C_3, i2c3_interrupt, 2);
#if defined(CHIP_FAMILY_MEC172X)
DECLARE_IRQ(MCHP_IRQ_I2C_4, i2c4_interrupt, 2);
#elif defined(CHIP_FAMILY_MEC152X)
DECLARE_IRQ(MCHP_IRQ_I2C_4, i2c4_interrupt, 2);
DECLARE_IRQ(MCHP_IRQ_I2C_5, i2c5_interrupt, 2);
DECLARE_IRQ(MCHP_IRQ_I2C_6, i2c6_interrupt, 2);
DECLARE_IRQ(MCHP_IRQ_I2C_7, i2c7_interrupt, 2);
#endif
