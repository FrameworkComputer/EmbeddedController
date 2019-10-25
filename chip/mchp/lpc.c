/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LPC module for MCHP MEC family */

#include "common.h"
#include "acpi.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "keyboard_protocol.h"
#include "lpc.h"
#include "lpc_chip.h"
#include "espi.h"
#include "port80.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "chipset.h"
#include "tfdp_chip.h"

/* Console output macros */
#ifdef CONFIG_MCHP_DEBUG_LPC
#define CPUTS(outstr) cputs(CC_LPC, outstr)
#define CPRINTS(format, args...) cprints(CC_LPC, format, ## args)
#else
#define CPUTS(...)
#define CPRINTS(...)
#endif

static uint8_t
mem_mapped[0x200] __attribute__((section(".bss.big_align")));

static struct host_packet lpc_packet;
static struct host_cmd_handler_args host_cmd_args;
static uint8_t host_cmd_flags;   /* Flags from host command */

static uint8_t params_copy[EC_LPC_HOST_PACKET_SIZE] __aligned(4);
static int init_done;

static struct ec_lpc_host_args * const lpc_host_args =
	(struct ec_lpc_host_args *)mem_mapped;

#ifdef CONFIG_BOARD_ID_CMD_ACPI_EC1
static uint8_t custom_acpi_cmd;
static uint8_t custom_acpi_ec2os_cnt;
static uint8_t custom_apci_ec2os[4];
#endif


static void keyboard_irq_assert(void)
{
#ifdef CONFIG_KEYBOARD_IRQ_GPIO
	/*
	 * Enforce signal-high for long enough for the signal to be
	 * pulled high by the external pullup resistor. This ensures the
	 * host will see the following falling edge, regardless of the
	 * line state before this function call.
	 */
	gpio_set_level(CONFIG_KEYBOARD_IRQ_GPIO, 1);
	udelay(4);
	/* Generate a falling edge */
	gpio_set_level(CONFIG_KEYBOARD_IRQ_GPIO, 0);
	udelay(4);

	/* Set signal high, now that we've generated the edge */
	gpio_set_level(CONFIG_KEYBOARD_IRQ_GPIO, 1);
#else
	/*
	 * SERIRQ is automatically sent by KBC
	 */
#endif
}

/**
 * Generate SMI pulse to the host chipset via GPIO.
 *
 * If the x86 is in S0, SMI# is sampled at 33MHz, so minimum pulse length
 * is 60ns. If the x86 is in S3, SMI# is sampled at 32.768KHz, so we need
 * pulse length >61us. Both are short enough and events are infrequent,
 * so just delay for 65us.
 */
static void lpc_generate_smi(void)
{
	CPUTS("LPC Pulse SMI");
#ifdef CONFIG_HOSTCMD_ESPI
	/* eSPI: pulse SMI# Virtual Wire low */
	espi_vw_pulse_wire(VW_SMI_L, 0);
#else
	gpio_set_level(GPIO_PCH_SMI_L, 0);
	udelay(65);
	gpio_set_level(GPIO_PCH_SMI_L, 1);
#endif
}

static void lpc_generate_sci(void)
{
	CPUTS("LPC Pulse SCI");
#ifdef CONFIG_SCI_GPIO
	gpio_set_level(CONFIG_SCI_GPIO, 0);
	udelay(65);
	gpio_set_level(CONFIG_SCI_GPIO, 1);
#else
#ifdef CONFIG_HOSTCMD_ESPI
	espi_vw_pulse_wire(VW_SCI_L, 0);
#else
	MCHP_ACPI_PM_STS |= 1;
	udelay(65);
	MCHP_ACPI_PM_STS &= ~1;
#endif
#endif
}

/**
 * Update the level-sensitive wake signal to the AP.
 *
 * @param wake_events	Currently asserted wake events
 */
static void lpc_update_wake(host_event_t wake_events)
{
	/*
	 * Mask off power button event, since the AP gets that
	 * through a separate dedicated GPIO.
	 */
	wake_events &= ~EC_HOST_EVENT_MASK(EC_HOST_EVENT_POWER_BUTTON);

#ifdef CONFIG_HOSTCMD_ESPI
	espi_vw_set_wire(VW_WAKE_L, !wake_events);
#else
	/* Signal is asserted low when wake events is non-zero */
	gpio_set_level(GPIO_PCH_WAKE_L, !wake_events);
#endif
}

static uint8_t *lpc_get_hostcmd_data_range(void)
{
	return mem_mapped;
}


/**
 * Update the host event status.
 *
 * Sends a pulse if masked event status becomes non-zero:
 *   - SMI pulse via PCH_SMI_L GPIO
 *   - SCI pulse via PCH_SCI_L GPIO
 */
void lpc_update_host_event_status(void)
{
	int need_sci = 0;
	int need_smi = 0;

	CPUTS("LPC update_host_event_status");

	if (!init_done)
		return;

	/* Disable LPC interrupt while updating status register */
	task_disable_irq(MCHP_IRQ_ACPIEC0_IBF);

	if (lpc_get_host_events_by_type(LPC_HOST_EVENT_SMI)) {
		/* Only generate SMI for first event */
		if (!(MCHP_ACPI_EC_STATUS(0) & EC_LPC_STATUS_SMI_PENDING))
			need_smi = 1;
		MCHP_ACPI_EC_STATUS(0) |= EC_LPC_STATUS_SMI_PENDING;
	} else {
		MCHP_ACPI_EC_STATUS(0) &= ~EC_LPC_STATUS_SMI_PENDING;
	}

	if (lpc_get_host_events_by_type(LPC_HOST_EVENT_SCI)) {
		/* Generate SCI for every event */
		need_sci = 1;
		MCHP_ACPI_EC_STATUS(0) |= EC_LPC_STATUS_SCI_PENDING;
	} else {
		MCHP_ACPI_EC_STATUS(0) &= ~EC_LPC_STATUS_SCI_PENDING;
	}

	/* Copy host events to mapped memory */
	*(uint32_t *)host_get_memmap(EC_MEMMAP_HOST_EVENTS) =
			lpc_get_host_events();

	task_enable_irq(MCHP_IRQ_ACPIEC0_IBF);

	/* Process the wake events. */
	lpc_update_wake(lpc_get_host_events_by_type(LPC_HOST_EVENT_WAKE));

	/* Send pulse on SMI signal if needed */
	if (need_smi)
		lpc_generate_smi();

	/* ACPI 5.0-12.6.1: Generate SCI for SCI_EVT=1. */
	if (need_sci)
		lpc_generate_sci();
}

static void lpc_send_response(struct host_cmd_handler_args *args)
{
	uint8_t *out;
	int size = args->response_size;
	int csum;
	int i;

	/* Ignore in-progress on LPC since interface is synchronous anyway */
	if (args->result == EC_RES_IN_PROGRESS)
		return;

	/* Handle negative size */
	if (size < 0) {
		args->result = EC_RES_INVALID_RESPONSE;
		size = 0;
	}

	/* New-style response */
	lpc_host_args->flags =
		(host_cmd_flags & ~EC_HOST_ARGS_FLAG_FROM_HOST) |
			EC_HOST_ARGS_FLAG_TO_HOST;

	lpc_host_args->data_size = size;

	csum = args->command + lpc_host_args->flags +
			lpc_host_args->command_version +
			lpc_host_args->data_size;

	for (i = 0, out = (uint8_t *)args->response; i < size; i++, out++)
		csum += *out;

	lpc_host_args->checksum = (uint8_t)csum;

	/* Fail if response doesn't fit in the param buffer */
	if (size > EC_PROTO2_MAX_PARAM_SIZE)
		args->result = EC_RES_INVALID_RESPONSE;

	/* Write result to the data byte. */
	MCHP_ACPI_EC_EC2OS(1, 0) = args->result;

	/*
	 * Clear processing flag in hardware and
         * sticky status in interrupt aggregator.
	 */
	MCHP_ACPI_EC_STATUS(1) &= ~EC_LPC_STATUS_PROCESSING;
	MCHP_INT_SOURCE(MCHP_ACPI_EC_GIRQ) =
				MCHP_ACPI_EC_IBF_GIRQ_BIT(1);

}

static void lpc_send_response_packet(struct host_packet *pkt)
{
	/* Ignore in-progress on LPC since interface is
	 * synchronous anyway
	 */
	if (pkt->driver_result == EC_RES_IN_PROGRESS) {
		/* CPRINTS("LPC EC_RES_IN_PROGRESS"); */
		return;
	}

	CPRINTS("LPC Set EC2OS(1,0)=0x%02x", pkt->driver_result);

	/* Write result to the data byte. */
	MCHP_ACPI_EC_EC2OS(1, 0) = pkt->driver_result;

	/* Clear the busy bit, so the host knows the EC is done. */
	MCHP_ACPI_EC_STATUS(1) &= ~EC_LPC_STATUS_PROCESSING;
	MCHP_INT_SOURCE(MCHP_ACPI_EC_GIRQ) =
				MCHP_ACPI_EC_IBF_GIRQ_BIT(1);
}

uint8_t *lpc_get_memmap_range(void)
{
	return mem_mapped + 0x100;
}

void lpc_mem_mapped_init(void)
{
	/* We support LPC args and version 3 protocol */
	*(lpc_get_memmap_range() + EC_MEMMAP_HOST_CMD_FLAGS) =
		EC_HOST_CMD_FLAG_LPC_ARGS_SUPPORTED |
		EC_HOST_CMD_FLAG_VERSION_3;
}

const int acpi_ec_pcr_slp[MCHP_ACPI_EC_MAX] = {
	MCHP_PCR_ACPI_EC0,
	MCHP_PCR_ACPI_EC1,
	MCHP_PCR_ACPI_EC2,
	MCHP_PCR_ACPI_EC3,
	MCHP_PCR_ACPI_EC4,
};

const int acpi_ec_nvic_ibf[MCHP_ACPI_EC_MAX] = {
	MCHP_IRQ_ACPIEC0_IBF,
	MCHP_IRQ_ACPIEC1_IBF,
	MCHP_IRQ_ACPIEC2_IBF,
	MCHP_IRQ_ACPIEC3_IBF,
	MCHP_IRQ_ACPIEC4_IBF,
};

#ifdef CONFIG_HOSTCMD_ESPI
const int acpi_ec_espi_bar_id[MCHP_ACPI_EC_MAX] = {
	MCHP_ESPI_IO_BAR_ID_ACPI_EC0,
	MCHP_ESPI_IO_BAR_ID_ACPI_EC1,
	MCHP_ESPI_IO_BAR_ID_ACPI_EC2,
	MCHP_ESPI_IO_BAR_ID_ACPI_EC3,
	MCHP_ESPI_IO_BAR_ID_ACPI_EC4,
};
#endif

void chip_acpi_ec_config(int instance, uint32_t io_base, uint8_t mask)
{
	if (instance >= MCHP_ACPI_EC_MAX)
		CPUTS("ACPI EC CFG invalid");

	MCHP_PCR_SLP_DIS_DEV(acpi_ec_pcr_slp[instance]);

#ifdef CONFIG_HOSTCMD_ESPI
	MCHP_ESPI_IO_BAR_CTL_MASK(acpi_ec_espi_bar_id[instance]) =
			mask;
	MCHP_ESPI_IO_BAR(acpi_ec_espi_bar_id[instance]) =
			(io_base << 16) + 0x01ul;
#else
	MCHP_LPC_ACPI_EC_BAR(instance) = (io_base << 16) +
		(1ul << 15) + mask;
#endif
	MCHP_ACPI_EC_STATUS(instance) &= ~EC_LPC_STATUS_PROCESSING;
	MCHP_INT_ENABLE(MCHP_ACPI_EC_GIRQ) =
			MCHP_ACPI_EC_IBF_GIRQ_BIT(instance);
	task_enable_irq(acpi_ec_nvic_ibf[instance]);
}

/*
 * 8042EM hardware decodes with fixed mask of 0x04
 * Example: io_base == 0x60 -> decodes 0x60/0x64
 * Enable both IBF and OBE interrupts.
 */
void chip_8042_config(uint32_t io_base)
{
	MCHP_PCR_SLP_DIS_DEV(MCHP_PCR_8042);

#ifdef CONFIG_HOSTCMD_ESPI
	MCHP_ESPI_IO_BAR_CTL_MASK(MCHP_ESPI_IO_BAR_ID_8042) = 0x04;
	MCHP_ESPI_IO_BAR(MCHP_ESPI_IO_BAR_ID_8042) =
			(io_base << 16) + 0x01ul;
#else
	/* Set up 8042 interface at 0x60/0x64 */
	MCHP_LPC_8042_BAR = (io_base << 16) + (1ul << 15);
#endif
	/* Set up indication of Auxiliary sts */
	MCHP_8042_KB_CTRL |= BIT(7);

	MCHP_8042_ACT |= 1;

	MCHP_INT_ENABLE(MCHP_8042_GIRQ) = MCHP_8042_OBE_GIRQ_BIT +
			MCHP_8042_IBF_GIRQ_BIT;

	task_enable_irq(MCHP_IRQ_8042EM_IBF);
	task_enable_irq(MCHP_IRQ_8042EM_OBE);

#ifndef CONFIG_KEYBOARD_IRQ_GPIO
	/* Set up SERIRQ for keyboard */
	MCHP_8042_KB_CTRL |= BIT(5);
	MCHP_LPC_SIRQ(1) = 0x01;
#endif
}

/*
 * Access data RAM
 * MCHP EMI Base address register = physical address of buffer
 * in SRAM. EMI hardware adds 16-bit offset Host programs into
 * EC_Address_LSB/MSB registers.
 * Limit EMI read / write range. First 256 bytes are RW for host
 * commands. Second 256 bytes are RO for mem-mapped data.
 * Hardware decodes a fixed 16 byte IO range.
 */
void chip_emi0_config(uint32_t io_base)
{
#ifdef CONFIG_HOSTCMD_ESPI
	MCHP_ESPI_IO_BAR_CTL_MASK(MCHP_ESPI_IO_BAR_ID_EMI0) = 0x0F;
	MCHP_ESPI_IO_BAR(MCHP_ESPI_IO_BAR_ID_EMI0) =
			(io_base << 16) + 0x01ul;
#else
	MCHP_LPC_EMI0_BAR = (io_base << 16) + (1ul << 15);
#endif

	MCHP_EMI_MBA0(0) = (uint32_t)mem_mapped;

	MCHP_EMI_MRL0(0) = 0x200;
	MCHP_EMI_MWL0(0) = 0x100;

	MCHP_INT_ENABLE(MCHP_EMI_GIRQ) = MCHP_EMI_GIRQ_BIT(0);
	task_enable_irq(MCHP_IRQ_EMI0);
}

/* Setup Port80 Debug Hardware ports.
 * First instance for I/O 80h only.
 * Clear FIFO's and timestamp.
 * Set FIFO interrupt threshold to maximum of 14 bytes.
 */
void chip_port80_config(uint32_t io_base)
{
	MCHP_PCR_SLP_DIS_DEV(MCHP_PCR_P80CAP0);

	MCHP_P80_CFG(0) = MCHP_P80_FLUSH_FIFO_WO +
			MCHP_P80_RESET_TIMESTAMP_WO;

#ifdef CONFIG_HOSTCMD_ESPI
	MCHP_ESPI_IO_BAR_CTL_MASK(MCHP_ESPI_IO_BAR_P80_0) = 0x00;
	MCHP_ESPI_IO_BAR(MCHP_ESPI_IO_BAR_P80_0) =
			(io_base << 16) + 0x01ul;
#else
	MCHP_LPC_P80DBG0_BAR = (io_base << 16) + (1ul << 15);
#endif
	MCHP_P80_CFG(0) = MCHP_P80_FIFO_THRHOLD_14 +
			MCHP_P80_TIMEBASE_1500KHZ +
			MCHP_P80_TIMER_ENABLE;

	MCHP_P80_ACTIVATE(0) = 1;

	MCHP_INT_SOURCE(15) = MCHP_INT15_P80(0);
	MCHP_INT_ENABLE(15) = MCHP_INT15_P80(0);
	task_enable_irq(MCHP_IRQ_PORT80DBG0);
}

#ifdef CONFIG_MCHP_DEBUG_LPC
static void chip_lpc_iobar_debug(void)
{
	CPRINTS("LPC ACPI EC0 IO BAR = 0x%08x", MCHP_LPC_ACPI_EC_BAR(0));
	CPRINTS("LPC ACPI EC1 IO BAR = 0x%08x", MCHP_LPC_ACPI_EC_BAR(1));
	CPRINTS("LPC 8042EM IO BAR   = 0x%08x", MCHP_LPC_8042_BAR);
	CPRINTS("LPC EMI0 IO BAR     = 0x%08x", MCHP_LPC_EMI0_BAR);
	CPRINTS("LPC Port80Dbg0 IO BAR = 0x%08x", MCHP_LPC_P80DBG0_BAR);
}
#endif

/*
 * Most registers in LPC module are reset when the host is off.
 * We need to set up LPC again when the host is starting up.
 * MCHP LRESET# can be one of two pins
 *	GPIO_0052 Func 2
 *	GPIO_0064 Func 1
 * Use GPIO interrupt to detect LRESET# changes.
 * Use GPIO_0064 for LRESET#. Must update board/board_name/gpio.inc
 *
 * For eSPI PLATFORM_RESET# virtual wire is used as LRESET#
 *
 */
#ifndef CONFIG_HOSTCMD_ESPI
static void setup_lpc(void)
{
	TRACE0(55, LPC, 0, "setup_lpc");

	MCHP_LPC_CFG_BAR |= (1ul << 15);

	/* Set up ACPI0 for 0x62/0x66 */
	chip_acpi_ec_config(0, 0x62, 0x04);

	/* Set up ACPI1 for 0x200 - 0x207 */
	chip_acpi_ec_config(1, 0x200, 0x07);

	/* Set up 8042 interface at 0x60/0x64 */
	chip_8042_config(0x60);

#ifndef CONFIG_KEYBOARD_IRQ_GPIO
	/* Set up SERIRQ for keyboard */
	MCHP_8042_KB_CTRL |= BIT(5);
	MCHP_LPC_SIRQ(1) = 0x01;
#endif
	/* EMI0 at IO 0x800 */
	chip_emi0_config(0x800);

	chip_port80_config(0x80);

	lpc_mem_mapped_init();

	/* Activate LPC interface */
	MCHP_LPC_ACT |= 1;

	/* Sufficiently initialized */
	init_done = 1;

	/* Update host events now that we can copy them to memmap */
	lpc_update_host_event_status();

#ifdef CONFIG_MCHP_DEBUG_LPC
	chip_lpc_iobar_debug();
#endif
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, setup_lpc, HOOK_PRIO_FIRST);
#endif

static void lpc_init(void)
{
	CPUTS("LPC HOOK_INIT");

	/* Initialize host args and memory map to all zero */
	memset(lpc_host_args, 0, sizeof(*lpc_host_args));
	memset(lpc_get_memmap_range(), 0, EC_MEMMAP_SIZE);

	/*
	 * Clear PCR sleep enables for peripherals we are using for
	 * both LPC and eSPI.
	 * Global Config, ACPI EC0/1, 8042 Keyboard controller,
	 * Port80 Capture0, and EMI.
	 * NOTE: EMI doesn't have a sleep enable.
	 */
	MCHP_PCR_SLP_DIS_DEV_MASK(2, MCHP_PCR_SLP_EN2_GCFG +
		MCHP_PCR_SLP_EN2_ACPI_EC0 +
		MCHP_PCR_SLP_EN2_ACPI_EC0 +
		MCHP_PCR_SLP_EN2_MIF8042);

	MCHP_PCR_SLP_DIS_DEV(MCHP_PCR_P80CAP0);

#ifdef CONFIG_HOSTCMD_ESPI

	espi_init();

#else
	/* Clear PCR LPC sleep enable */
	MCHP_PCR_SLP_DIS_DEV(MCHP_PCR_LPC);

	/* configure pins */
	gpio_config_module(MODULE_LPC, 1);

	/*
	 * MCHP LRESET# interrupt is GPIO interrupt
	 * and configured by GPIO table in board level gpio.inc
	 * Refer to lpcrst_interrupt() in this file.
	 */
	gpio_enable_interrupt(GPIO_PCH_PLTRST_L);

	/*
	 * b[8]=1(LRESET# is platform reset), b[0]=0 VCC_PWRGD is
	 * asserted when LRESET# is 1(inactive)
	 */
	MCHP_PCR_PWR_RST_CTL = 0x100ul;

	/*
	 * Allow LPC sleep if Host CLKRUN# signals
	 * clock stop and there are no pending SERIRQ
	 * or LPC DMA.
	 */
	MCHP_LPC_EC_CLK_CTRL =
		(MCHP_LPC_EC_CLK_CTRL & ~(0x03ul)) | 0x01ul;

	setup_lpc();
#endif
}
/*
 * Set priority to higher than default; this way LPC memory mapped
 * data is ready before other inits try to initialize their
 * memmap data.
 */
DECLARE_HOOK(HOOK_INIT, lpc_init, HOOK_PRIO_INIT_LPC);

#ifdef CONFIG_CHIPSET_RESET_HOOK
static void lpc_chipset_reset(void)
{
	hook_notify(HOOK_CHIPSET_RESET);
}
DECLARE_DEFERRED(lpc_chipset_reset);
#endif

void lpc_set_init_done(int val)
{
	init_done = val;
}

/*
 * MCHP MCHP family allows selecting one of two GPIO pins alternate
 * functions as LRESET#.
 * LRESET# can be monitored as bit[1](read-only) of the
 * LPC Bus Monitor register. NOTE: Bus Monitor is synchronized with
 * LPC clock. We have observed APL configurations where LRESET#
 * changes while LPC clock is not running!
 * bit[1]==0 -> LRESET# is high
 * bit[1]==1 -> LRESET# is low (active)
 * LRESET# active causes the EC to activate internal signal RESET_HOST.
 * MCHP_PCR_PWR_RST_STS bit[3](read-only) = RESET_HOST_STATUS =
 * 0 = Reset active
 * 1 = Reset not active
 * MCHP is different than MEC1322 in that LRESET# is not connected
 * to a separate interrupt source.
 * If using LPC the board design must select on of the two GPIO pins
 * dedicated for LRESET# and this pin must be configured in the
 * board level gpio.inc
 */
void lpcrst_interrupt(enum gpio_signal signal)
{
#ifndef CONFIG_HOSTCMD_ESPI
	/* Initialize LPC module when LRESET# is deasserted */
	if (!lpc_get_pltrst_asserted()) {
		setup_lpc();
	} else {
		/* Store port 80 reset event */
		port_80_write(PORT_80_EVENT_RESET);

#ifdef CONFIG_CHIPSET_RESET_HOOK
		/* Notify HOOK_CHIPSET_RESET */
		hook_call_deferred(&lpc_chipset_reset_data, MSEC);
#endif
	}
#ifdef CONFIG_MCHP_DEBUG_LPC
	CPRINTS("LPC RESET# %sasserted",
		lpc_get_pltrst_asserted() ? "" : "de");
#endif
#endif
}

/*
 * TODO - Is this only for debug of EMI host communication
 * or logging of EMI host communication? We don't observe
 * this ISR so Host is not writing to MCHP_EMI_H2E_MBX(0).
 */
void emi0_interrupt(void)
{
	uint8_t h2e;

	h2e = MCHP_EMI_H2E_MBX(0);
	CPRINTS("LPC Host 0x%02x -> EMI0 H2E(0)", h2e);
	port_80_write(h2e);
}
DECLARE_IRQ(MCHP_IRQ_EMI0, emi0_interrupt, 1);

/*
 * ISR empties BIOS Debug 0 FIFO and
 * writes data to circular buffer. How can we be
 * sure this routine can read the last Port 80h byte?
 */
int port_80_read(void)
{
	int data;

	data = PORT_80_IGNORE;
	if (MCHP_P80_STS(0) & MCHP_P80_STS_NOT_EMPTY)
		data = MCHP_P80_CAP(0) & 0xFF;

	return data;
}

#ifdef CONFIG_BOARD_ID_CMD_ACPI_EC1
/*
 * Handle custom ACPI EC0 commands.
 * Some chipset's CoreBoot will send read board ID command expecting
 * a two byte response.
 */
static int acpi_ec0_custom(int is_cmd, uint8_t value,
		uint8_t *resultptr)
{
	int rval;

	rval = 0;
	custom_acpi_ec2os_cnt = 0;
	*resultptr = 0x00;

	if (is_cmd && (value == 0x0d)) {
		MCHP_INT_SOURCE(MCHP_ACPI_EC_GIRQ) =
				MCHP_ACPI_EC_OBE_GIRQ_BIT(0);
		/* Write two bytes sequence 0xC2, 0x04 to Host */
		if (MCHP_ACPI_EC_BYTE_CTL(0) & 0x01) {
			/* Host enabled 4-byte mode */
			MCHP_ACPI_EC_EC2OS(0, 0) = 0x02;
			MCHP_ACPI_EC_EC2OS(0, 1) = 0x04;
			MCHP_ACPI_EC_EC2OS(0, 2) = 0x00;
			/* Sets OBF */
			MCHP_ACPI_EC_EC2OS(0, 3) = 0x00;
		} else {
			/* single byte mode */
			*resultptr = 0x02;
			custom_acpi_ec2os_cnt = 1;
			custom_apci_ec2os[0] = 0x04;
			MCHP_ACPI_EC_EC2OS(0, 0) = 0x02;
			MCHP_INT_ENABLE(MCHP_ACPI_EC_GIRQ) =
					MCHP_ACPI_EC_OBE_GIRQ_BIT(0);
			task_enable_irq(MCHP_IRQ_ACPIEC0_OBE);
		}
		custom_acpi_cmd = 0;
		rval = 1;
	}

	return rval;
}
#endif

void acpi_0_interrupt(void)
{
	uint8_t value, result, is_cmd;

	is_cmd = MCHP_ACPI_EC_STATUS(0);

	/* Set the bust bi */
	MCHP_ACPI_EC_STATUS(0) |= EC_LPC_STATUS_PROCESSING;

	result = MCHP_ACPI_EC_BYTE_CTL(0);

	/* Read command/data; this clears the FRMH bit. */
	value = MCHP_ACPI_EC_OS2EC(0, 0);

	is_cmd &= EC_LPC_STATUS_LAST_CMD;

	/* Handle whatever this was. */
	result = 0;
	if (acpi_ap_to_ec(is_cmd, value, &result))
		MCHP_ACPI_EC_EC2OS(0, 0) = result;
#ifdef CONFIG_BOARD_ID_CMD_ACPI_EC1
	else
		acpi_ec0_custom(is_cmd, value, &result);
#endif
	/* Clear the busy bit */
	MCHP_ACPI_EC_STATUS(0) &= ~EC_LPC_STATUS_PROCESSING;

	/* Clear R/W1C status bit in Aggregator */
	MCHP_INT_SOURCE(MCHP_ACPI_EC_GIRQ) =
			MCHP_ACPI_EC_IBF_GIRQ_BIT(0);

	/*
	 * ACPI 5.0-12.6.1: Generate SCI for Input Buffer Empty /
	 * Output Buffer Full condition on the kernel channel/
	 */
	lpc_generate_sci();
}
DECLARE_IRQ(MCHP_IRQ_ACPIEC0_IBF, acpi_0_interrupt, 1);

#ifdef CONFIG_BOARD_ID_CMD_ACPI_EC1
/*
 * ACPI EC0 output buffer empty ISR.
 * Used to handle custom ACPI EC0 command requiring
 * two byte response.
 */
void acpi_0_obe_isr(void)
{
	uint8_t sts, data;

	MCHP_INT_SOURCE(MCHP_ACPI_EC_GIRQ) =
			MCHP_ACPI_EC_OBE_GIRQ_BIT(0);

	sts = MCHP_ACPI_EC_STATUS(0);
	data = MCHP_ACPI_EC_BYTE_CTL(0);
	data = sts;
	if (custom_acpi_ec2os_cnt) {
		custom_acpi_ec2os_cnt--;
		data = custom_apci_ec2os[custom_acpi_ec2os_cnt];
	}

	if (custom_acpi_ec2os_cnt == 0) { /* was last byte? */
		MCHP_INT_DISABLE(MCHP_ACPI_EC_GIRQ) =
				MCHP_ACPI_EC_OBE_GIRQ_BIT(0);
	}

	lpc_generate_sci();
}
DECLARE_IRQ(MCHP_IRQ_ACPIEC0_OBE, acpi_0_obe_isr, 1);
#endif

void acpi_1_interrupt(void)
{
	uint8_t st = MCHP_ACPI_EC_STATUS(1);

	if (!(st & EC_LPC_STATUS_FROM_HOST) ||
	    !(st & EC_LPC_STATUS_LAST_CMD))
		return;

	/* Set the busy bit */
	MCHP_ACPI_EC_STATUS(1) |= EC_LPC_STATUS_PROCESSING;

	/*
	 * Read the command byte.  This clears the FRMH bit in
	 * the status byte.
	 */
	host_cmd_args.command = MCHP_ACPI_EC_OS2EC(1, 0);

	host_cmd_args.result = EC_RES_SUCCESS;
	host_cmd_args.send_response = lpc_send_response;
	host_cmd_flags = lpc_host_args->flags;

	/* We only support new style command (v3) now */
	if (host_cmd_args.command == EC_COMMAND_PROTOCOL_3) {
		lpc_packet.send_response = lpc_send_response_packet;

		lpc_packet.request =
			(const void *)lpc_get_hostcmd_data_range();
		lpc_packet.request_temp = params_copy;
		lpc_packet.request_max = sizeof(params_copy);
		/* Don't know the request size so
		 * pass in the entire buffer
		 */
		lpc_packet.request_size = EC_LPC_HOST_PACKET_SIZE;

		lpc_packet.response =
			(void *)lpc_get_hostcmd_data_range();
		lpc_packet.response_max = EC_LPC_HOST_PACKET_SIZE;
		lpc_packet.response_size = 0;

		lpc_packet.driver_result = EC_RES_SUCCESS;

		host_packet_receive(&lpc_packet);

	} else {
		/* Old style command unsupported */
		host_cmd_args.result = EC_RES_INVALID_COMMAND;

		/* Hand off to host command handler */
		host_command_received(&host_cmd_args);
	}
}
DECLARE_IRQ(MCHP_IRQ_ACPIEC1_IBF, acpi_1_interrupt, 1);

#ifdef HAS_TASK_KEYPROTO
/*
 * Reading data out of input buffer clears read-only status
 * in 8042EM. Next, we must clear aggregator status.
 */
void kb_ibf_interrupt(void)
{
	if (lpc_keyboard_input_pending())
		keyboard_host_write(MCHP_8042_H2E,
				    MCHP_8042_STS & BIT(3));

	MCHP_INT_SOURCE(MCHP_8042_GIRQ) = MCHP_8042_IBF_GIRQ_BIT;
	task_wake(TASK_ID_KEYPROTO);
}
DECLARE_IRQ(MCHP_IRQ_8042EM_IBF, kb_ibf_interrupt, 1);

/*
 * Interrupt generated when Host reads data byte from 8042EM
 * output buffer. The 8042EM STATUS.OBF bit will clear when the
 * Host reads the data and assert its OBE signal to interrupt
 * aggregator. Clear aggregator 8042EM OBE R/WC status bit before
 * invoking task.
 */
void kb_obe_interrupt(void)
{
	MCHP_INT_SOURCE(MCHP_8042_GIRQ) = MCHP_8042_OBE_GIRQ_BIT;
	task_wake(TASK_ID_KEYPROTO);
}
DECLARE_IRQ(MCHP_IRQ_8042EM_OBE, kb_obe_interrupt, 1);
#endif

/*
 * Bit 0 of 8042EM STATUS register is OBF meaning EC has written
 * data to EC2HOST data register. OBF is cleared when the host
 * reads the data.
 */
int lpc_keyboard_has_char(void)
{
	return (MCHP_8042_STS & BIT(0)) ? 1 : 0;
}

int lpc_keyboard_input_pending(void)
{
	return (MCHP_8042_STS & BIT(1)) ? 1 : 0;
}

/*
 * called from common/keyboard_8042.c
 */
void lpc_keyboard_put_char(uint8_t chr, int send_irq)
{
	MCHP_8042_E2H = chr;
	if (send_irq)
		keyboard_irq_assert();
}

/*
 * Read 8042 register and write to read-only register
 * insuring compiler does not optimize out the read.
 */
void lpc_keyboard_clear_buffer(void)
{
	MCHP_PCR_CHIP_OSC_ID = MCHP_8042_OBF_CLR;
}

void lpc_keyboard_resume_irq(void)
{
	if (lpc_keyboard_has_char())
		keyboard_irq_assert();
}

void lpc_set_acpi_status_mask(uint8_t mask)
{
	MCHP_ACPI_EC_STATUS(0) |= mask;
}

void lpc_clear_acpi_status_mask(uint8_t mask)
{
	MCHP_ACPI_EC_STATUS(0) &= ~mask;
}

/*
 * Read hardware to determine state of platform reset signal.
 * LPC issue: Observed APL chipset changing LRESET# while LPC
 * clock is not running. This violates original LPC specification.
 * Unable to find information in APL chipset documentation
 * stating APL can change LRESET# with LPC clock not running.
 * Could this be a CoreBoot issue during CB LPC configuration?
 * We work-around this issue by reading the GPIO state.
 */
int lpc_get_pltrst_asserted(void)
{
#ifdef CONFIG_HOSTCMD_ESPI
	/*
	 * eSPI PLTRST# a VWire or side-band signal
	 * Controlled by CONFIG_HOSTCMD_ESPI
	 */
	return !espi_vw_get_wire(VW_PLTRST_L);
#else
	/* returns 1 if LRESET# pin is asserted(low) else 0 */
#ifdef CONFIG_CHIPSET_APL_GLK
	/* Use GPIO */
	return !gpio_get_level(GPIO_PCH_PLTRST_L);
#else
	/* assumes LPC clock is running when host changes LRESET# */
	return (MCHP_LPC_BUS_MONITOR & (1<<1)) ? 1 : 0;
#endif
#endif
}

/* Enable LPC ACPI-EC0 interrupts */
void lpc_enable_acpi_interrupts(void)
{
	task_enable_irq(MCHP_IRQ_ACPIEC0_IBF);
}

/* Disable LPC ACPI-EC0 interrupts */
void lpc_disable_acpi_interrupts(void)
{
	task_disable_irq(MCHP_IRQ_ACPIEC0_IBF);
}

/* On boards without a host, this command is used to set up LPC */
static int lpc_command_init(int argc, char **argv)
{
	lpc_init();
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(lpcinit, lpc_command_init, NULL, NULL);

/* Get protocol information */
static enum ec_status lpc_get_protocol_info(struct host_cmd_handler_args *args)
{
	struct ec_response_get_protocol_info *r = args->response;

	CPUTS("MEC1701 Handler EC_CMD_GET_PROTOCOL_INFO");

	memset(r, 0, sizeof(*r));
	r->protocol_versions = BIT(3);
	r->max_request_packet_size = EC_LPC_HOST_PACKET_SIZE;
	r->max_response_packet_size = EC_LPC_HOST_PACKET_SIZE;
	r->flags = 0;

	args->response_size = sizeof(*r);

	return EC_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_PROTOCOL_INFO,
		lpc_get_protocol_info,
		EC_VER_MASK(0));

#ifdef CONFIG_MCHP_DEBUG_LPC
static int command_lpc(int argc, char **argv)
{
	if (argc == 1)
		return EC_ERROR_PARAM1;

	if (!strcasecmp(argv[1], "sci"))
		lpc_generate_sci();
	else if (!strcasecmp(argv[1], "smi"))
		lpc_generate_smi();
	else if (!strcasecmp(argv[1], "wake"))
		lpc_update_wake(-1);
	else
		return EC_ERROR_PARAM1;
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(lpc, command_lpc, "[sci|smi|wake]",
	"Trigger SCI/SMI");
#endif

