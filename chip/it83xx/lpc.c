/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LPC module for Chrome EC */

#include "acpi.h"
#include "chipset.h"
#include "clock.h"
#include "common.h"
#include "console.h"
#include "ec2i_chip.h"
#include "espi.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "intc.h"
#include "irq_chip.h"
#include "keyboard_protocol.h"
#include "lpc.h"
#include "port80.h"
#include "pwm.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_LPC, outstr)
#define CPRINTS(format, args...) cprints(CC_LPC, format, ##args)

/* LPC PM channels */
enum lpc_pm_ch {
	LPC_PM1 = 0,
	LPC_PM2,
	LPC_PM3,
	LPC_PM4,
	LPC_PM5,
};

enum pm_ctrl_mask {
	/* Input Buffer Full Interrupt Enable. */
	PM_CTRL_IBFIE = 0x01,
	/* Output Buffer Empty Interrupt Enable. */
	PM_CTRL_OBEIE = 0x02,
};

#define LPC_ACPI_CMD LPC_PM1 /* ACPI commands 62h/66h port */
#define LPC_HOST_CMD LPC_PM2 /* Host commands 200h/204h port */
#define LPC_HOST_PORT_80H LPC_PM3 /* Host 80h port */

static uint8_t acpi_ec_memmap[EC_MEMMAP_SIZE]
	__attribute__((section(".h2ram.pool.acpiec")));
static uint8_t host_cmd_memmap[256]
	__attribute__((section(".h2ram.pool.hostcmd")));

static struct host_packet lpc_packet;
static struct host_cmd_handler_args host_cmd_args;
static uint8_t host_cmd_flags; /* Flags from host command */

/* Params must be 32-bit aligned */
static uint8_t params_copy[EC_LPC_HOST_PACKET_SIZE] __aligned(4);
static int init_done;
static int p80l_index;

static struct ec_lpc_host_args *const lpc_host_args =
	(struct ec_lpc_host_args *)host_cmd_memmap;

static void pm_set_ctrl(enum lpc_pm_ch ch, enum pm_ctrl_mask ctrl, int set)
{
	if (set)
		IT83XX_PMC_PMCTL(ch) |= ctrl;
	else
		IT83XX_PMC_PMCTL(ch) &= ~ctrl;
}

static void pm_set_status(enum lpc_pm_ch ch, uint8_t status, int set)
{
	if (set)
		IT83XX_PMC_PMSTS(ch) |= status;
	else
		IT83XX_PMC_PMSTS(ch) &= ~status;
}

static uint8_t pm_get_status(enum lpc_pm_ch ch)
{
	return IT83XX_PMC_PMSTS(ch);
}

static uint8_t pm_get_data_in(enum lpc_pm_ch ch)
{
	return IT83XX_PMC_PMDI(ch);
}

static void pm_put_data_out(enum lpc_pm_ch ch, uint8_t out)
{
	IT83XX_PMC_PMDO(ch) = out;
}

static void pm_clear_ibf(enum lpc_pm_ch ch)
{
	/* bit7, write-1 clear IBF */
	IT83XX_PMC_PMIE(ch) |= BIT(7);
}

#ifdef CONFIG_KEYBOARD_IRQ_GPIO
static void keyboard_irq_assert(void)
{
	/*
	 * Enforce signal-high for long enough for the signal to be pulled high
	 * by the external pullup resistor.  This ensures the host will see the
	 * following falling edge, regardless of the line state before this
	 * function call.
	 */
	gpio_set_level(CONFIG_KEYBOARD_IRQ_GPIO, 1);
	udelay(4);
	/* Generate a falling edge */
	gpio_set_level(CONFIG_KEYBOARD_IRQ_GPIO, 0);
	udelay(4);

	/* Set signal high, now that we've generated the edge */
	gpio_set_level(CONFIG_KEYBOARD_IRQ_GPIO, 1);
}
#endif

/**
 * Generate SMI pulse to the host chipset via GPIO.
 *
 * If the x86 is in S0, SMI# is sampled at 33MHz, so minimum pulse length is
 * 60ns.  If the x86 is in S3, SMI# is sampled at 32.768KHz, so we need pulse
 * length >61us.  Both are short enough and events are infrequent, so just
 * delay for 65us.
 */
static void lpc_generate_smi(void)
{
#ifdef CONFIG_HOST_INTERFACE_ESPI
	espi_vw_set_wire(VW_SMI_L, 0);
	udelay(CONFIG_HOST_INTERFACE_ESPI_DEFAULT_VW_WIDTH_US);
	espi_vw_set_wire(VW_SMI_L, 1);
#else
	gpio_set_level(GPIO_PCH_SMI_L, 0);
	udelay(65);
	gpio_set_level(GPIO_PCH_SMI_L, 1);
#endif
}

static void lpc_generate_sci(void)
{
#ifdef CONFIG_HOST_INTERFACE_ESPI
	espi_vw_set_wire(VW_SCI_L, 0);
	udelay(CONFIG_HOST_INTERFACE_ESPI_DEFAULT_VW_WIDTH_US);
	espi_vw_set_wire(VW_SCI_L, 1);
#else
	gpio_set_level(GPIO_PCH_SCI_L, 0);
	udelay(65);
	gpio_set_level(GPIO_PCH_SCI_L, 1);
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
	 * Mask off power button event, since the AP gets that through a
	 * separate dedicated GPIO.
	 */
	wake_events &= ~EC_HOST_EVENT_MASK(EC_HOST_EVENT_POWER_BUTTON);

	/* Signal is asserted low when wake events is non-zero */
	gpio_set_level(GPIO_PCH_WAKE_L, !wake_events);
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
	lpc_host_args->flags = (host_cmd_flags & ~EC_HOST_ARGS_FLAG_FROM_HOST) |
			       EC_HOST_ARGS_FLAG_TO_HOST;

	lpc_host_args->data_size = size;

	csum = args->command + lpc_host_args->flags +
	       lpc_host_args->command_version + lpc_host_args->data_size;

	for (i = 0, out = (uint8_t *)args->response; i < size; i++, out++)
		csum += *out;

	lpc_host_args->checksum = (uint8_t)csum;

	/* Fail if response doesn't fit in the param buffer */
	if (size > EC_PROTO2_MAX_PARAM_SIZE)
		args->result = EC_RES_INVALID_RESPONSE;

	/* Write result to the data byte.  This sets the OBF status bit. */
	pm_put_data_out(LPC_HOST_CMD, args->result);

	/* Clear the busy bit, so the host knows the EC is done. */
	pm_set_status(LPC_HOST_CMD, EC_LPC_STATUS_PROCESSING, 0);
}

void lpc_update_host_event_status(void)
{
	int need_sci = 0;
	int need_smi = 0;

	if (!init_done)
		return;

	/* Disable PMC1 interrupt while updating status register */
	task_disable_irq(IT83XX_IRQ_PMC_IN);

	if (lpc_get_host_events_by_type(LPC_HOST_EVENT_SMI)) {
		/* Only generate SMI for first event */
		if (!(pm_get_status(LPC_ACPI_CMD) & EC_LPC_STATUS_SMI_PENDING))
			need_smi = 1;
		pm_set_status(LPC_ACPI_CMD, EC_LPC_STATUS_SMI_PENDING, 1);
	} else {
		pm_set_status(LPC_ACPI_CMD, EC_LPC_STATUS_SMI_PENDING, 0);
	}

	if (lpc_get_host_events_by_type(LPC_HOST_EVENT_SCI)) {
		/* Generate SCI for every event */
		need_sci = 1;
		pm_set_status(LPC_ACPI_CMD, EC_LPC_STATUS_SCI_PENDING, 1);
	} else {
		pm_set_status(LPC_ACPI_CMD, EC_LPC_STATUS_SCI_PENDING, 0);
	}

	/* Copy host events to mapped memory */
	*(host_event_t *)host_get_memmap(EC_MEMMAP_HOST_EVENTS) =
		lpc_get_host_events();

	task_enable_irq(IT83XX_IRQ_PMC_IN);

	/* Process the wake events. */
	lpc_update_wake(lpc_get_host_events_by_type(LPC_HOST_EVENT_WAKE));

	/* Send pulse on SMI signal if needed */
	if (need_smi)
		lpc_generate_smi();

	/* ACPI 5.0-12.6.1: Generate SCI for SCI_EVT=1. */
	if (need_sci)
		lpc_generate_sci();
}

static void lpc_send_response_packet(struct host_packet *pkt)
{
	/* Ignore in-progress on LPC since interface is synchronous anyway */
	if (pkt->driver_result == EC_RES_IN_PROGRESS)
		return;

	/* Write result to the data byte. */
	pm_put_data_out(LPC_HOST_CMD, pkt->driver_result);

	/* Clear the busy bit, so the host knows the EC is done. */
	pm_set_status(LPC_HOST_CMD, EC_LPC_STATUS_PROCESSING, 0);
}

uint8_t *lpc_get_memmap_range(void)
{
	return (uint8_t *)acpi_ec_memmap;
}

int lpc_keyboard_has_char(void)
{
	/* OBE or OBF */
	return IT83XX_KBC_KBHISR & 0x01;
}

int lpc_keyboard_input_pending(void)
{
	/* IBE or IBF */
	return IT83XX_KBC_KBHISR & 0x02;
}

void lpc_keyboard_put_char(uint8_t chr, int send_irq)
{
	/* Clear programming data bit 7-4 */
	IT83XX_KBC_KBHISR &= 0x0F;

	/* keyboard */
	IT83XX_KBC_KBHISR |= 0x10;

#ifdef CONFIG_KEYBOARD_IRQ_GPIO
	task_clear_pending_irq(IT83XX_IRQ_KBC_OUT);
	/* The data output to the KBC Data Output Register. */
	IT83XX_KBC_KBHIKDOR = chr;
	task_enable_irq(IT83XX_IRQ_KBC_OUT);
	if (send_irq)
		keyboard_irq_assert();
#else
	/*
	 * bit0 = 0, The IRQ1 is controlled by the IRQ1B bit in KBIRQR.
	 * bit1 = 0, The IRQ12 is controlled by the IRQ12B bit in KBIRQR.
	 */
	IT83XX_KBC_KBHICR &= 0x3C;

	/*
	 * Enable the interrupt to keyboard driver in the host processor
	 * via SERIRQ when the output buffer is full.
	 */
	if (send_irq)
		IT83XX_KBC_KBHICR |= 0x01;

	udelay(16);

	task_clear_pending_irq(IT83XX_IRQ_KBC_OUT);
	/* The data output to the KBC Data Output Register. */
	IT83XX_KBC_KBHIKDOR = chr;
	task_enable_irq(IT83XX_IRQ_KBC_OUT);
#endif
}

void lpc_keyboard_clear_buffer(void)
{
	uint32_t int_mask = read_clear_int_mask();

	/* bit6, write-1 clear OBF */
	IT83XX_KBC_KBHICR |= BIT(6);
	IT83XX_KBC_KBHICR &= ~BIT(6);
	set_int_mask(int_mask);
}

void lpc_keyboard_resume_irq(void)
{
	if (lpc_keyboard_has_char()) {
#ifdef CONFIG_KEYBOARD_IRQ_GPIO
		keyboard_irq_assert();
#else
		/* The IRQ1 is controlled by the IRQ1B bit in KBIRQR. */
		IT83XX_KBC_KBHICR &= ~0x01;

		/*
		 * When the OBFKIE bit in KBC Host Interface Control Register
		 * (KBHICR) is 0, the bit directly controls the IRQ1 signal.
		 */
		IT83XX_KBC_KBIRQR |= 0x01;
#endif

		task_clear_pending_irq(IT83XX_IRQ_KBC_OUT);

		task_enable_irq(IT83XX_IRQ_KBC_OUT);
	}
}

void lpc_set_acpi_status_mask(uint8_t mask)
{
	pm_set_status(LPC_ACPI_CMD, mask, 1);
}

void lpc_clear_acpi_status_mask(uint8_t mask)
{
	pm_set_status(LPC_ACPI_CMD, mask, 0);
}

#ifndef CONFIG_HOST_INTERFACE_ESPI
int lpc_get_pltrst_asserted(void)
{
	return !gpio_get_level(GPIO_PCH_PLTRST_L);
}
#endif

#ifdef HAS_TASK_KEYPROTO
/* KBC and PMC control modules */
void lpc_kbc_ibf_interrupt(void)
{
	if (lpc_keyboard_input_pending()) {
		keyboard_host_write(IT83XX_KBC_KBHIDIR,
				    (IT83XX_KBC_KBHISR & 0x08) ? 1 : 0);
		/* bit7, write-1 clear IBF */
		IT83XX_KBC_KBHICR |= BIT(7);
		IT83XX_KBC_KBHICR &= ~BIT(7);
	}

	task_clear_pending_irq(IT83XX_IRQ_KBC_IN);

	task_wake(TASK_ID_KEYPROTO);
}

void lpc_kbc_obe_interrupt(void)
{
	task_disable_irq(IT83XX_IRQ_KBC_OUT);

	task_clear_pending_irq(IT83XX_IRQ_KBC_OUT);

#ifndef CONFIG_KEYBOARD_IRQ_GPIO
	if (!(IT83XX_KBC_KBHICR & 0x01)) {
		IT83XX_KBC_KBIRQR &= ~0x01;

		IT83XX_KBC_KBHICR |= 0x01;
	}
#endif

	task_wake(TASK_ID_KEYPROTO);
}
#endif /* HAS_TASK_KEYPROTO */

void pm1_ibf_interrupt(void)
{
	int is_cmd;
	uint8_t value, result;

	if (pm_get_status(LPC_ACPI_CMD) & EC_LPC_STATUS_FROM_HOST) {
		/* Set the busy bit */
		pm_set_status(LPC_ACPI_CMD, EC_LPC_STATUS_PROCESSING, 1);

		/* data from command port or data port */
		is_cmd = pm_get_status(LPC_ACPI_CMD) & EC_LPC_STATUS_LAST_CMD;

		/* Get command or data */
		value = pm_get_data_in(LPC_ACPI_CMD);

		/* Handle whatever this was. */
		if (acpi_ap_to_ec(is_cmd, value, &result))
			pm_put_data_out(LPC_ACPI_CMD, result);

		pm_clear_ibf(LPC_ACPI_CMD);

		/* Clear the busy bit */
		pm_set_status(LPC_ACPI_CMD, EC_LPC_STATUS_PROCESSING, 0);

		/*
		 * ACPI 5.0-12.6.1: Generate SCI for Input Buffer Empty
		 * Output Buffer Full condition on the kernel channel.
		 */
		lpc_generate_sci();
	}

	task_clear_pending_irq(IT83XX_IRQ_PMC_IN);
}

void pm2_ibf_interrupt(void)
{
	uint8_t value __attribute__((unused)) = 0;
	uint8_t status;

	status = pm_get_status(LPC_HOST_CMD);
	/* IBE */
	if (!(status & EC_LPC_STATUS_FROM_HOST)) {
		task_clear_pending_irq(IT83XX_IRQ_PMC2_IN);
		return;
	}

	/* IBF and data port */
	if (!(status & EC_LPC_STATUS_LAST_CMD)) {
		/* R/C IBF*/
		value = pm_get_data_in(LPC_HOST_CMD);
		pm_clear_ibf(LPC_HOST_CMD);
		task_clear_pending_irq(IT83XX_IRQ_PMC2_IN);
		return;
	}

	/* Set the busy bit */
	pm_set_status(LPC_HOST_CMD, EC_LPC_STATUS_PROCESSING, 1);

	/*
	 * Read the command byte.  This clears the FRMH bit in
	 * the status byte.
	 */
	host_cmd_args.command = pm_get_data_in(LPC_HOST_CMD);

	host_cmd_args.result = EC_RES_SUCCESS;
	if (host_cmd_args.command != EC_COMMAND_PROTOCOL_3)
		host_cmd_args.send_response = lpc_send_response;
	host_cmd_flags = lpc_host_args->flags;

	/* We only support new style command (v3) now */
	if (host_cmd_args.command == EC_COMMAND_PROTOCOL_3) {
		lpc_packet.send_response = lpc_send_response_packet;

		lpc_packet.request = (const void *)host_cmd_memmap;
		lpc_packet.request_temp = params_copy;
		lpc_packet.request_max = sizeof(params_copy);
		/* Don't know the request size so pass in the entire buffer */
		lpc_packet.request_size = EC_LPC_HOST_PACKET_SIZE;

		lpc_packet.response = (void *)host_cmd_memmap;
		lpc_packet.response_max = EC_LPC_HOST_PACKET_SIZE;
		lpc_packet.response_size = 0;

		lpc_packet.driver_result = EC_RES_SUCCESS;
		host_packet_receive(&lpc_packet);

		pm_clear_ibf(LPC_HOST_CMD);
		task_clear_pending_irq(IT83XX_IRQ_PMC2_IN);
		return;
	} else {
		/* Old style command, now unsupported */
		host_cmd_args.result = EC_RES_INVALID_COMMAND;
	}

	/* Hand off to host command handler */
	host_command_received(&host_cmd_args);

	pm_clear_ibf(LPC_HOST_CMD);
	task_clear_pending_irq(IT83XX_IRQ_PMC2_IN);
}

void pm3_ibf_interrupt(void)
{
	int new_p80_idx, i;
	enum ec2i_message ec2i_r;

	/* set LDN */
	if (ec2i_write(HOST_INDEX_LDN, LDN_RTCT) == EC2I_WRITE_SUCCESS) {
		/* get P80L current index */
		ec2i_r = ec2i_read(HOST_INDEX_DSLDC6);
		/* clear IBF */
		pm_clear_ibf(LPC_HOST_PORT_80H);
		/* read OK */
		if ((ec2i_r & 0xff00) == EC2I_READ_SUCCESS) {
			new_p80_idx = ec2i_r & P80L_BRAM_BANK1_SIZE_MASK;
			for (i = 0; i < (P80L_P80LE - P80L_P80LB + 1); i++) {
				if (++p80l_index > P80L_P80LE)
					p80l_index = P80L_P80LB;
				port_80_write(IT83XX_BRAM_BANK1(p80l_index));
				if (p80l_index == new_p80_idx)
					break;
			}
		}
	} else {
		pm_clear_ibf(LPC_HOST_PORT_80H);
	}

	task_clear_pending_irq(IT83XX_IRQ_PMC3_IN);
}

void pm4_ibf_interrupt(void)
{
	pm_clear_ibf(LPC_PM4);
	task_clear_pending_irq(IT83XX_IRQ_PMC4_IN);
}

void pm5_ibf_interrupt(void)
{
	pm_clear_ibf(LPC_PM5);
	task_clear_pending_irq(IT83XX_IRQ_PMC5_IN);
}

static void lpc_init(void)
{
	enum ec2i_message ec2i_r;

	/* SPI peripheral interface is disabled */
	IT83XX_GCTRL_SSCR = 0;
	/*
	 * DLM 52k~56k size select enable.
	 * For mapping LPC I/O cycle 800h ~ 9FFh to DLM 8D800 ~ 8D9FF.
	 */
	IT83XX_GCTRL_MCCR2 |= 0x10;

	/* The register pair to access PNPCFG is 004Eh and 004Fh */
	IT83XX_GCTRL_BADRSEL = 0x01;

	/* Disable KBC IRQ */
	IT83XX_KBC_KBIRQR = 0x00;

	/*
	 * bit2, Output Buffer Empty CPU Interrupt Enable.
	 * bit3, Input Buffer Full CPU Interrupt Enable.
	 * bit5, IBF/OBF EC clear mode.
	 *   0b: IBF cleared if EC read data register, EC reset, or host reset.
	 *       OBF cleared if host read data register, or EC reset.
	 *   1b: IBF cleared if EC write-1 to bit7 at related registers,
	 *       EC reset, or host reset.
	 *       OBF cleared if host read data register, EC write-1 to bit6 at
	 *       related registers, or EC reset.
	 */
	IT83XX_KBC_KBHICR |= 0x2C;

	/* PM1 Input Buffer Full Interrupt Enable for 62h/66 port */
	pm_set_ctrl(LPC_ACPI_CMD, PM_CTRL_IBFIE, 1);

	/* PM2 Input Buffer Full Interrupt Enable for 200h/204 port */
	pm_set_ctrl(LPC_HOST_CMD, PM_CTRL_IBFIE, 1);

	memset(lpc_get_memmap_range(), 0, EC_MEMMAP_SIZE);
	memset(lpc_host_args, 0, sizeof(*lpc_host_args));

	/* Host LPC I/O cycle mapping to RAM */
#ifdef IT83XX_H2RAM_REMAPPING
	/*
	 * On it8xxx2 series, host I/O cycles are mapped to the first block
	 * (0x80080000~0x80080fff) at default, and it is adjustable.
	 * We should set the correct offset depends on the base address of
	 * H2RAM section, so EC will be able to receive/handle commands from
	 * host.
	 */
	IT83XX_GCTRL_H2ROFSR =
		(CONFIG_H2RAM_BASE - CONFIG_RAM_BASE) / CONFIG_H2RAM_SIZE;
#endif
	/*
	 * bit[4], H2RAM through LPC IO cycle.
	 * bit[1], H2RAM window 1 enabled.
	 * bit[0], H2RAM window 0 enabled.
	 */
	IT83XX_SMFI_HRAMWC |= 0x13;

	/*
	 * bit[7:6]
	 * Host RAM Window[x] Read Protect Enable
	 * 00b: Disabled
	 * 01b: Lower half of RAM window protected
	 * 10b: Upper half of RAM window protected
	 * 11b: All protected
	 *
	 * bit[5:4]
	 * Host RAM Window[x] Write Protect Enable
	 * 00b: Disabled
	 * 01b: Lower half of RAM window protected
	 * 10b: Upper half of RAM window protected
	 * 11b: All protected
	 *
	 * bit[2:0]
	 * Host RAM Window 1 Size (HRAMW1S)
	 * 0h: 16 bytes
	 * 1h: 32 bytes
	 * 2h: 64 bytes
	 * 3h: 128 bytes
	 * 4h: 256 bytes
	 * 5h: 512 bytes
	 * 6h: 1024 bytes
	 * 7h: 2048 bytes
	 */

	/* H2RAM Win 0 Base Address 800h allow r/w for host_cmd_memmap */
	IT83XX_SMFI_HRAMW0BA = 0x80;
	IT83XX_SMFI_HRAMW0AAS = 0x04;

	/* H2RAM Win 1 Base Address 900h allow r for acpi_ec_memmap */
	IT83XX_SMFI_HRAMW1BA = 0x90;
	IT83XX_SMFI_HRAMW1AAS = 0x34;

	/* We support LPC args and version 3 protocol */
	*(lpc_get_memmap_range() + EC_MEMMAP_HOST_CMD_FLAGS) =
		EC_HOST_CMD_FLAG_LPC_ARGS_SUPPORTED |
		EC_HOST_CMD_FLAG_VERSION_3;

	/*
	 * bit[5], Dedicated interrupt
	 * INT3: PMC1 Output Buffer Empty Int
	 * INT25: PMC1 Input Buffer Full Int
	 * INT26: PMC2 Output Buffer Empty Int
	 * INT27: PMC2 Input Buffer Full Int
	 */
	IT83XX_PMC_MBXCTRL |= 0x20;

	/* PM3 Input Buffer Full Interrupt Enable for 80h port */
	pm_set_ctrl(LPC_HOST_PORT_80H, PM_CTRL_IBFIE, 1);

	p80l_index = P80L_P80LC;
	if (ec2i_write(HOST_INDEX_LDN, LDN_RTCT) == EC2I_WRITE_SUCCESS) {
		/* get P80L current index */
		ec2i_r = ec2i_read(HOST_INDEX_DSLDC6);
		/* read OK */
		if ((ec2i_r & 0xff00) == EC2I_READ_SUCCESS)
			p80l_index = ec2i_r & P80L_BRAM_BANK1_SIZE_MASK;
	}

	/*
	 * bit[7], enable P80L function.
	 * bit[6], accept port 80h cycle.
	 * bit[1-0], 10b: I2EC is read-only.
	 */
	IT83XX_GCTRL_SPCTRL1 |= 0xC2;

#ifndef CONFIG_HOST_INTERFACE_ESPI
	gpio_enable_interrupt(GPIO_PCH_PLTRST_L);
#endif

#ifdef HAS_TASK_KEYPROTO
	task_clear_pending_irq(IT83XX_IRQ_KBC_OUT);
	task_disable_irq(IT83XX_IRQ_KBC_OUT);

	task_clear_pending_irq(IT83XX_IRQ_KBC_IN);
	task_enable_irq(IT83XX_IRQ_KBC_IN);
#endif

	task_clear_pending_irq(IT83XX_IRQ_PMC_IN);
	pm_set_status(LPC_ACPI_CMD, EC_LPC_STATUS_PROCESSING, 0);
	task_enable_irq(IT83XX_IRQ_PMC_IN);

	task_clear_pending_irq(IT83XX_IRQ_PMC2_IN);
	pm_set_status(LPC_HOST_CMD, EC_LPC_STATUS_PROCESSING, 0);
	task_enable_irq(IT83XX_IRQ_PMC2_IN);

	task_clear_pending_irq(IT83XX_IRQ_PMC3_IN);
	task_enable_irq(IT83XX_IRQ_PMC3_IN);

#ifdef CONFIG_HOST_INTERFACE_ESPI
	espi_init();
#endif
	/* Sufficiently initialized */
	init_done = 1;

	/* Update host events now that we can copy them to memmap */
	lpc_update_host_event_status();
}
/*
 * Set prio to higher than default; this way LPC memory mapped data is ready
 * before other inits try to initialize their memmap data.
 */
DECLARE_HOOK(HOOK_INIT, lpc_init, HOOK_PRIO_INIT_LPC);

#ifndef CONFIG_HOST_INTERFACE_ESPI
void lpcrst_interrupt(enum gpio_signal signal)
{
	if (lpc_get_pltrst_asserted())
		/* Store port 80 reset event */
		port_80_write(PORT_80_EVENT_RESET);

	CPRINTS("LPC RESET# %sasserted", lpc_get_pltrst_asserted() ? "" : "de");
}
#endif

/* Enable LPC ACPI-EC interrupts */
void lpc_enable_acpi_interrupts(void)
{
	task_enable_irq(IT83XX_IRQ_PMC_IN);
}

/* Disable LPC ACPI-EC interrupts */
void lpc_disable_acpi_interrupts(void)
{
	task_disable_irq(IT83XX_IRQ_PMC_IN);
}

/* Get protocol information */
static enum ec_status lpc_get_protocol_info(struct host_cmd_handler_args *args)
{
	struct ec_response_get_protocol_info *r = args->response;

	memset(r, 0, sizeof(*r));
	r->protocol_versions = BIT(3);
	r->max_request_packet_size = EC_LPC_HOST_PACKET_SIZE;
	r->max_response_packet_size = EC_LPC_HOST_PACKET_SIZE;
	r->flags = 0;

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_PROTOCOL_INFO, lpc_get_protocol_info,
		     EC_VER_MASK(0));
