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
#include "espi.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i8042_protocol.h"
#include "keyboard_protocol.h"
#include "lpc.h"
#include "lpc_chip.h"
#include "port80.h"
#include "registers.h"
#include "sib_chip.h"
#include "system.h"
#include "system_chip.h"
#include "task.h"
#include "uart.h"
#include "util.h"

/* Console output macros */
#if !(DEBUG_LPC)
#define CPUTS(...)
#define CPRINTS(...)
#else
#define CPUTS(outstr) cputs(CC_LPC, outstr)
#define CPRINTS(format, args...) cprints(CC_LPC, format, ##args)
#endif

/* PM channel definitions */
#define PMC_ACPI PM_CHAN_1
#define PMC_HOST_CMD PM_CHAN_2

/* Microseconds to wait for eSPI VW changes to propagate */
#define ESPI_DIRTY_WAIT_TIME_US 150

#define PORT80_MAX_BUF_SIZE 16
static uint16_t port80_buf[PORT80_MAX_BUF_SIZE];

static struct host_packet lpc_packet;
static struct host_cmd_handler_args host_cmd_args;
static uint8_t host_cmd_flags; /* Flags from host command */
static uint8_t shm_mem_host_cmd[256] __aligned(8);
static uint8_t shm_memmap[256] __aligned(8);
/* Params must be 32-bit aligned */
static uint8_t params_copy[EC_LPC_HOST_PACKET_SIZE] __aligned(4);
static int init_done;

static struct ec_lpc_host_args *const lpc_host_args =
	(struct ec_lpc_host_args *)shm_mem_host_cmd;

/*****************************************************************************/
/* IC specific low-level driver */
static void keyboard_irq_assert(void)
{
#ifdef CONFIG_KEYBOARD_IRQ_GPIO
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
#else
	/*
	 * SERIRQ is automatically sent by KBC
	 */
#endif
}

static void lpc_task_enable_irq(void)
{
#ifdef HAS_TASK_KEYPROTO
	task_enable_irq(NPCX_IRQ_KBC_IBF);
#endif
	task_enable_irq(NPCX_IRQ_PM_CHAN_IBF);
	task_enable_irq(NPCX_IRQ_PORT80);
#ifdef CONFIG_HOST_INTERFACE_ESPI
	task_enable_irq(NPCX_IRQ_ESPI);
	/* Virtual Wire: SLP_S3/4/5, SUS_STAT, PLTRST, OOB_RST_WARN */
	task_enable_irq(NPCX_IRQ_WKINTA_2);
	/* Virtual Wire: HOST_RST_WARN, SUS_WARN, SUS_PWRDN_ACK, SLP_A */
	task_enable_irq(NPCX_IRQ_WKINTB_2);
	/* Enable eSPI module interrupts and wake-up functionalities */
	NPCX_ESPIIE |= (ESPIIE_GENERIC | ESPIIE_VW);
	NPCX_ESPIWE |= (ESPIWE_GENERIC | ESPIWE_VW);
#endif
}

static void lpc_task_disable_irq(void)
{
#ifdef HAS_TASK_KEYPROTO
	task_disable_irq(NPCX_IRQ_KBC_IBF);
#endif
	task_disable_irq(NPCX_IRQ_PM_CHAN_IBF);
	task_disable_irq(NPCX_IRQ_PORT80);
#ifdef CONFIG_HOST_INTERFACE_ESPI
	task_disable_irq(NPCX_IRQ_ESPI);
	/* Virtual Wire: SLP_S3/4/5, SUS_STAT, PLTRST, OOB_RST_WARN */
	task_disable_irq(NPCX_IRQ_WKINTA_2);
	/* Virtual Wire: HOST_RST_WARN,SUS_WARN, SUS_PWRDN_ACK, SLP_A */
	task_disable_irq(NPCX_IRQ_WKINTB_2);
	/* Disable eSPI module interrupts and wake-up functionalities */
	NPCX_ESPIIE &= ~(ESPIIE_GENERIC | ESPIIE_VW);
	NPCX_ESPIWE &= ~(ESPIWE_GENERIC | ESPIWE_VW);
#endif
}
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
	host_event_t smi;

#ifdef CONFIG_SCI_GPIO
	/* Enforce signal-high for long enough to debounce high */
	gpio_set_level(GPIO_PCH_SMI_L, 1);
	udelay(65);
	/* Generate a falling edge */
	gpio_set_level(GPIO_PCH_SMI_L, 0);
	udelay(65);
	/* Set signal high, now that we've generated the edge */
	gpio_set_level(GPIO_PCH_SMI_L, 1);
#elif defined(CONFIG_HOST_INTERFACE_ESPI)
	/*
	 * Don't use SET_BIT/CLEAR_BIT macro to toggle SMIB/SCIB to generate
	 * virtual wire. Use NPCX_VW_SMI/NPCX_VW_SCI macro instead.
	 * The reason is - if GPIOC6/CPIO76 are not selected as SMI/SCI, reading
	 * from SMIB/SCIB doesn't really reflect the SMI/SCI status. SMI/SCI
	 * status should be read from bit 1/0 in eSPI VMEVSM(2) register.
	 */
	/* Generate a falling edge */
	espi_wait_vw_not_dirty(VW_SMI_L, ESPI_DIRTY_WAIT_TIME_US);
	NPCX_HIPMIC(PMC_ACPI) = NPCX_VW_SMI(0);
	udelay(CONFIG_HOST_INTERFACE_ESPI_DEFAULT_VW_WIDTH_US);
	espi_wait_vw_not_dirty(VW_SMI_L, ESPI_DIRTY_WAIT_TIME_US);

	/* Set signal high */
	NPCX_HIPMIC(PMC_ACPI) = NPCX_VW_SMI(1);
#else
	/* SET SMIB bit to pull SMI_L to high.*/
	SET_BIT(NPCX_HIPMIC(PMC_ACPI), NPCX_HIPMIC_SMIB);
	udelay(CONFIG_HOST_INTERFACE_ESPI_DEFAULT_VW_WIDTH_US);
	/* Generate a falling edge */
	CLEAR_BIT(NPCX_HIPMIC(PMC_ACPI), NPCX_HIPMIC_SMIB);
	udelay(CONFIG_HOST_INTERFACE_ESPI_DEFAULT_VW_WIDTH_US);
	/* Set signal high */
	SET_BIT(NPCX_HIPMIC(PMC_ACPI), NPCX_HIPMIC_SMIB);
#endif
	smi = lpc_get_host_events_by_type(LPC_HOST_EVENT_SMI);
	if (smi)
		HOST_EVENT_CPRINTS("smi", smi);
}

/**
 * Generate SCI pulse to the host chipset via LPC0SCI.
 */
static void lpc_generate_sci(void)
{
	host_event_t sci;

#ifdef CONFIG_SCI_GPIO
	/* Enforce signal-high for long enough to debounce high */
	gpio_set_level(CONFIG_SCI_GPIO, 1);
	udelay(65);
	/* Generate a falling edge */
	gpio_set_level(CONFIG_SCI_GPIO, 0);
	udelay(65);
	/* Set signal high, now that we've generated the edge */
	gpio_set_level(CONFIG_SCI_GPIO, 1);
#elif defined(CONFIG_HOST_INTERFACE_ESPI)
	/*
	 * Don't use SET_BIT/CLEAR_BIT macro to toggle SMIB/SCIB to generate
	 * virtual wire. Use NPCX_VW_SMI/NPCX_VW_SCI macro instead.
	 * The reason is - if GPIOC6/CPIO76 are not selected as SMI/SCI, reading
	 * from SMIB/SCIB doesn't really reflect the SMI/SCI status. SMI/SCI
	 * status should be read from bit 1/0 in eSPI VMEVSM(2) register.
	 */
	/* Generate a falling edge */
	espi_wait_vw_not_dirty(VW_SCI_L, ESPI_DIRTY_WAIT_TIME_US);
	NPCX_HIPMIC(PMC_ACPI) = NPCX_VW_SCI(0);
	udelay(CONFIG_HOST_INTERFACE_ESPI_DEFAULT_VW_WIDTH_US);
	espi_wait_vw_not_dirty(VW_SCI_L, ESPI_DIRTY_WAIT_TIME_US);

	/* Set signal high */
	NPCX_HIPMIC(PMC_ACPI) = NPCX_VW_SCI(1);
#else
	/* Set SCIB bit to pull SCI_L to high.*/
	SET_BIT(NPCX_HIPMIC(PMC_ACPI), NPCX_HIPMIC_SCIB);
	udelay(CONFIG_HOST_INTERFACE_ESPI_DEFAULT_VW_WIDTH_US);
	/* Generate a falling edge */
	CLEAR_BIT(NPCX_HIPMIC(PMC_ACPI), NPCX_HIPMIC_SCIB);
	udelay(CONFIG_HOST_INTERFACE_ESPI_DEFAULT_VW_WIDTH_US);
	/* Set signal high */
	SET_BIT(NPCX_HIPMIC(PMC_ACPI), NPCX_HIPMIC_SCIB);
#endif

	sci = lpc_get_host_events_by_type(LPC_HOST_EVENT_SCI);
	if (sci)
		HOST_EVENT_CPRINTS("sci", sci);
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

uint8_t *lpc_get_memmap_range(void)
{
	return (uint8_t *)shm_memmap;
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

	/* Write result to the data byte.  This sets the TOH status bit. */
	NPCX_HIPMDO(PMC_HOST_CMD) = args->result;
	/* Clear processing flag */
	CLEAR_BIT(NPCX_HIPMST(PMC_HOST_CMD), NPCX_HIPMST_F0);
}

static void lpc_send_response_packet(struct host_packet *pkt)
{
	/* Ignore in-progress on LPC since interface is synchronous anyway */
	if (pkt->driver_result == EC_RES_IN_PROGRESS)
		return;

	/* Write result to the data byte.  This sets the TOH status bit. */
	NPCX_HIPMDO(PMC_HOST_CMD) = pkt->driver_result;
	/* Clear processing flag */
	CLEAR_BIT(NPCX_HIPMST(PMC_HOST_CMD), NPCX_HIPMST_F0);
}

int lpc_keyboard_has_char(void)
{
	/* if OBF bit is '1', that mean still have a data in DBBOUT */
	return (NPCX_HIKMST & 0x01) ? 1 : 0;
}

int lpc_keyboard_input_pending(void)
{
	/* if IBF bit is '1', that mean still have a data in DBBIN */
	return (NPCX_HIKMST & 0x02) ? 1 : 0;
}

/* Put a char to host buffer by HIKDO and send IRQ if specified. */
void lpc_keyboard_put_char(uint8_t chr, int send_irq)
{
	NPCX_HIKDO = chr;
	CPRINTS("KB put %02x", chr);

	/* Enable OBE interrupt to detect host read data out */
	SET_BIT(NPCX_HICTRL, NPCX_HICTRL_OBECIE);
	task_enable_irq(NPCX_IRQ_KBC_OBE);
	if (send_irq) {
		keyboard_irq_assert();
	}
}

/* Put an aux char to host buffer by HIMDO and assert status bit 5. */
void lpc_aux_put_char(uint8_t chr, int send_irq)
{
	if (send_irq)
		SET_BIT(NPCX_HICTRL, NPCX_HICTRL_OBFMIE);
	else
		CLEAR_BIT(NPCX_HICTRL, NPCX_HICTRL_OBFMIE);

	NPCX_HIKMST |= I8042_AUX_DATA;
	NPCX_HIMDO = chr;
	CPRINTS("AUX put %02x", chr);

	/* Enable OBE interrupt to detect host read data out */
	SET_BIT(NPCX_HICTRL, NPCX_HICTRL_OBECIE);
	task_enable_irq(NPCX_IRQ_KBC_OBE);
}

void lpc_keyboard_clear_buffer(void)
{
	/*
	 * Only npcx5 series need this bypass. The bug of FW_OBF is fixed in
	 * npcx7 series and later npcx ec.
	 */
#ifdef CHIP_FAMILY_NPCX5
	/* Clear OBF flag in host STATUS and HIKMST regs */
	if (IS_BIT_SET(NPCX_HIKMST, NPCX_HIKMST_OBF)) {
		/*
		 * Setting HICTRL.FW_OBF clears the HIKMST.OBF and STATUS.OBF
		 * but it does not deassert IRQ1 when it was already asserted.
		 * Emulate a host read to clear these two flags and also
		 * deassert IRQ1
		 */
		sib_read_kbc_reg(0x0);
	}
#else
	/* Make sure the previous TOH and IRQ has been sent out. */
	udelay(4);
	/* Clear OBE flag in host STATUS  and HIKMST regs*/
	SET_BIT(NPCX_HICTRL, NPCX_HICTRL_FW_OBF);
	/* Ensure there is no TOH set in this period. */
	udelay(4);
#endif
}

void lpc_keyboard_resume_irq(void)
{
	if (lpc_keyboard_has_char())
		keyboard_irq_assert();
}

/**
 * Update the host event status.
 *
 * Sends a pulse if masked event status becomes non-zero:
 *   - SMI pulse via EC_SMI_L GPIO
 *   - SCI pulse via LPC0SCI
 */
void lpc_update_host_event_status(void)
{
	int need_sci = 0;
	int need_smi = 0;

	if (!init_done)
		return;

	/* Disable LPC interrupt while updating status register */
	lpc_task_disable_irq();
	if (lpc_get_host_events_by_type(LPC_HOST_EVENT_SMI)) {
		/* Only generate SMI for first event */
		if (!(NPCX_HIPMST(PMC_ACPI) & NPCX_HIPMST_ST2))
			need_smi = 1;
		SET_BIT(NPCX_HIPMST(PMC_ACPI), NPCX_HIPMST_ST2);
	} else
		CLEAR_BIT(NPCX_HIPMST(PMC_ACPI), NPCX_HIPMST_ST2);

	if (lpc_get_host_events_by_type(LPC_HOST_EVENT_SCI)) {
		/* Generate SCI for every event */
		need_sci = 1;
		SET_BIT(NPCX_HIPMST(PMC_ACPI), NPCX_HIPMST_ST1);
	} else
		CLEAR_BIT(NPCX_HIPMST(PMC_ACPI), NPCX_HIPMST_ST1);

	/* Copy host events to mapped memory */
	*(host_event_t *)host_get_memmap(EC_MEMMAP_HOST_EVENTS) =
		lpc_get_host_events();

	lpc_task_enable_irq();

	/* Process the wake events. */
	lpc_update_wake(lpc_get_host_events_by_type(LPC_HOST_EVENT_WAKE));

	/* Send pulse on SMI signal if needed */
	if (need_smi)
		lpc_generate_smi();

	/* ACPI 5.0-12.6.1: Generate SCI for SCI_EVT=1. */
	if (need_sci)
		lpc_generate_sci();
}

void lpc_set_acpi_status_mask(uint8_t mask)
{
	NPCX_HIPMST(PMC_ACPI) |= mask;
}

void lpc_clear_acpi_status_mask(uint8_t mask)
{
	NPCX_HIPMST(PMC_ACPI) &= ~mask;
}

/* Enable LPC ACPI-EC interrupts */
void lpc_enable_acpi_interrupts(void)
{
	SET_BIT(NPCX_HIPMCTL(PMC_ACPI), NPCX_HIPMCTL_IBFIE);
}

/* Disable LPC ACPI-EC interrupts */
void lpc_disable_acpi_interrupts(void)
{
	CLEAR_BIT(NPCX_HIPMCTL(PMC_ACPI), NPCX_HIPMCTL_IBFIE);
}

/**
 * Handle write to ACPI I/O port
 *
 * @param is_cmd	Is write command (is_cmd=1) or data (is_cmd=0)
 */
static void handle_acpi_write(int is_cmd)
{
	uint8_t value, result;

	/* Set processing flag before reading command byte */
	SET_BIT(NPCX_HIPMST(PMC_ACPI), NPCX_HIPMST_F0);

	/* Read command/data; this clears the FRMH status bit. */
	value = NPCX_HIPMDI(PMC_ACPI);

	/* Handle whatever this was. */
	if (acpi_ap_to_ec(is_cmd, value, &result))
		NPCX_HIPMDO(PMC_ACPI) = result;

	/* Clear processing flag */
	CLEAR_BIT(NPCX_HIPMST(PMC_ACPI), NPCX_HIPMST_F0);

	/*
	 * ACPI 5.0-12.6.1: Generate SCI for Input Buffer Empty / Output Buffer
	 * Full condition on the kernel channel.
	 */
	lpc_generate_sci();
}

/**
 * Handle write to host command I/O ports.
 *
 * @param is_cmd	Is write command (1) or data (0)?
 */
static void handle_host_write(int is_cmd)
{
	/* Set processing flag before reading command byte */
	SET_BIT(NPCX_HIPMST(PMC_HOST_CMD), NPCX_HIPMST_F0);
	/*
	 * Read the command byte.  This clears the FRMH bit in
	 * the status byte.
	 */
	host_cmd_args.command = NPCX_HIPMDI(PMC_HOST_CMD);

	host_cmd_args.result = EC_RES_SUCCESS;
	host_cmd_args.send_response = lpc_send_response;
	host_cmd_flags = lpc_host_args->flags;

	/* See if we have an old or new style command */
	if (host_cmd_args.command == EC_COMMAND_PROTOCOL_3) {
		lpc_packet.send_response = lpc_send_response_packet;

		lpc_packet.request = (const void *)shm_mem_host_cmd;
		lpc_packet.request_temp = params_copy;
		lpc_packet.request_max = sizeof(params_copy);
		/* Don't know the request size so pass in the entire buffer */
		lpc_packet.request_size = EC_LPC_HOST_PACKET_SIZE;

		lpc_packet.response = (void *)shm_mem_host_cmd;
		lpc_packet.response_max = EC_LPC_HOST_PACKET_SIZE;
		lpc_packet.response_size = 0;

		lpc_packet.driver_result = EC_RES_SUCCESS;

		host_packet_receive(&lpc_packet);
		return;

	} else {
		/* Old style command, now unsupported */
		host_cmd_args.result = EC_RES_INVALID_COMMAND;
	}

	/* Hand off to host command handler */
	host_command_received(&host_cmd_args);
}

/*****************************************************************************/
/* Interrupt handlers */
#ifdef HAS_TASK_KEYPROTO
/* KB controller input buffer full ISR */
static void lpc_kbc_ibf_interrupt(void)
{
	uint8_t status;
	uint8_t ibf;
	/* If "command" input 0, else 1*/
	if (lpc_keyboard_input_pending()) {
		/*
		 * Reading HIKMDI causes the IBF flag to deassert and allows
		 * the host to write a new byte into the input buffer. So if we
		 * don't capture the status before reading HIKMDI we will race
		 * with the host and get an invalid value for HIKMST.A20.
		 */
		status = NPCX_HIKMST;
		ibf = NPCX_HIKMDI;
		keyboard_host_write(ibf, (status & 0x08) ? 1 : 0);
		CPRINTS("ibf isr %02x", ibf);
		task_wake(TASK_ID_KEYPROTO);
	} else {
		CPRINTS("ibf isr spurious");
	}
}
DECLARE_IRQ(NPCX_IRQ_KBC_IBF, lpc_kbc_ibf_interrupt, 4);

/* KB controller output buffer empty ISR */
static void lpc_kbc_obe_interrupt(void)
{
	/* Disable KBC OBE interrupt */
	CLEAR_BIT(NPCX_HICTRL, NPCX_HICTRL_OBECIE);
	task_disable_irq(NPCX_IRQ_KBC_OBE);

	CPRINTS("obe isr %02x", NPCX_HIKMST);

	NPCX_HIKMST &= ~I8042_AUX_DATA;

	task_wake(TASK_ID_KEYPROTO);
}
DECLARE_IRQ(NPCX_IRQ_KBC_OBE, lpc_kbc_obe_interrupt, 4);
#endif

/* PM channel input buffer full ISR */
static void lpc_pmc_ibf_interrupt(void)
{
	/* Channel-1 for ACPI usage*/
	/* Channel-2 for Host Command usage , so the argument data had been
	 * put on the share memory firstly*/
	if (NPCX_HIPMST(PMC_ACPI) & 0x02)
		handle_acpi_write((NPCX_HIPMST(PMC_ACPI) & 0x08) ? 1 : 0);
	else if (NPCX_HIPMST(PMC_HOST_CMD) & 0x02)
		handle_host_write((NPCX_HIPMST(PMC_HOST_CMD) & 0x08) ? 1 : 0);
}
DECLARE_IRQ(NPCX_IRQ_PM_CHAN_IBF, lpc_pmc_ibf_interrupt, 4);

/* PM channel output buffer empty ISR */
static void lpc_pmc_obe_interrupt(void)
{
}
DECLARE_IRQ(NPCX_IRQ_PM_CHAN_OBE, lpc_pmc_obe_interrupt, 4);

static void lpc_port80_interrupt(void)
{
	uint8_t i;
	uint8_t count = 0;
	uint32_t code = 0;

	/* buffer Port80 data to the local buffer if FIFO is not empty */
	while (IS_BIT_SET(NPCX_DP80STS, NPCX_DP80STS_FNE) &&
	       (count < ARRAY_SIZE(port80_buf)))
		port80_buf[count++] = NPCX_DP80BUF;

	for (i = 0; i < count; i++) {
		uint8_t offset;
		uint32_t buf_data;

		buf_data = port80_buf[i];
		offset = GET_FIELD(buf_data, NPCX_DP80BUF_OFFS_FIELD);
		code |= (buf_data & 0xFF) << (8 * offset);

		if (i == count - 1) {
			port_80_write(code);
			break;
		}

		/* peek the offset of the next byte */
		buf_data = port80_buf[i + 1];
		offset = GET_FIELD(buf_data, NPCX_DP80BUF_OFFS_FIELD);
		/*
		 * If the peeked next byte's offset is 0 means it is the start
		 * of the new code. Pass the current code to Port80
		 * common layer.
		 */
		if (offset == 0) {
			port_80_write(code);
			code = 0;
		}
	}

	/* If FIFO is overflow */
	if (IS_BIT_SET(NPCX_DP80STS, NPCX_DP80STS_FOR)) {
		SET_BIT(NPCX_DP80STS, NPCX_DP80STS_FOR);
		CPRINTS("DP80 FIFO Overflow!");
	}

	/* Clear pending bit of host writing */
	SET_BIT(NPCX_DP80STS, NPCX_DP80STS_FWR);
}
DECLARE_IRQ(NPCX_IRQ_PORT80, lpc_port80_interrupt, 4);

/**
 * Preserve event masks across a sysjump.
 */
static void lpc_sysjump(void)
{
	lpc_task_disable_irq();

	/* Disable protect for Win 1 and 2. */
	NPCX_WIN_WR_PROT(0) = 0;
	NPCX_WIN_WR_PROT(1) = 0;
	NPCX_WIN_RD_PROT(0) = 0;
	NPCX_WIN_RD_PROT(1) = 0;

	/* Reset base address for Win 1 and 2. */
	NPCX_WIN_BASE(0) = 0xfffffff8;
	NPCX_WIN_BASE(1) = 0xfffffff8;
}
DECLARE_HOOK(HOOK_SYSJUMP, lpc_sysjump, HOOK_PRIO_DEFAULT);

/* For LPC host register initial via SIB module */
void host_register_init(void)
{
	/* Enable Core-to-Host Modules Access */
	SET_BIT(NPCX_SIBCTRL, NPCX_SIBCTRL_CSAE);

	/* enable ACPI*/
	sib_write_reg(SIO_OFFSET, 0x07, 0x11);
	sib_write_reg(SIO_OFFSET, 0x30, 0x01);

	/* Enable kbc and mouse */
#ifdef HAS_TASK_KEYPROTO
	/* LDN = 0x06 : keyboard */
	sib_write_reg(SIO_OFFSET, 0x07, 0x06);
#ifdef CONFIG_NCPX_KBC_IRQ_ACTIVE_LOW
	sib_write_reg(SIO_OFFSET, 0x71, 0x01);
#endif
	sib_write_reg(SIO_OFFSET, 0x30, 0x01);

	/* LDN = 0x05 : mouse */
	if (IS_ENABLED(CONFIG_PS2)) {
		sib_write_reg(SIO_OFFSET, 0x07, 0x05);
		sib_write_reg(SIO_OFFSET, 0x30, 0x01);
	}
#endif

	/* Setting PMC2 */
	/* LDN register = 0x12(PMC2) */
	sib_write_reg(SIO_OFFSET, 0x07, 0x12);
	/* CMD port is 0x200 */
	sib_write_reg(SIO_OFFSET, 0x60, 0x02);
	sib_write_reg(SIO_OFFSET, 0x61, 0x00);
	/* Data port is 0x204 */
	sib_write_reg(SIO_OFFSET, 0x62, 0x02);
	sib_write_reg(SIO_OFFSET, 0x63, 0x04);
	/* enable PMC2 */
	sib_write_reg(SIO_OFFSET, 0x30, 0x01);

	/* Setting SHM */
	/* LDN register = 0x0F(SHM) */
	sib_write_reg(SIO_OFFSET, 0x07, 0x0F);
	/* WIN1&2 mapping to IO */
	sib_write_reg(SIO_OFFSET, 0xF1, sib_read_reg(SIO_OFFSET, 0xF1) | 0x30);
	/* WIN1 as Host Command on the IO:0x0800 */
	sib_write_reg(SIO_OFFSET, 0xF5, 0x08);
	sib_write_reg(SIO_OFFSET, 0xF4, 0x00);
	/* WIN2 as MEMMAP on the IO:0x900 */
	sib_write_reg(SIO_OFFSET, 0xF9, 0x09);
	sib_write_reg(SIO_OFFSET, 0xF8, 0x00);

	/*
	 * eSPI allows sending 4 bytes of Port80 code in a single PUT_IOWR_SHORT
	 * transaction. When setting OFS0_SEL~OFS3_SEL in DPAR1 register to 1,
	 * EC hardware will put those 4 bytes of Port80 code to DP80BUF FIFO.
	 * This is only supported when CHIP_FAMILY >= NPCX9.
	 */
	if (IS_ENABLED(CONFIG_HOST_INTERFACE_ESPI))
		sib_write_reg(SIO_OFFSET, 0xFD, 0x0F);
	/* enable SHM */
	sib_write_reg(SIO_OFFSET, 0x30, 0x01);

	CPRINTS("Host settings are done!");
}

#ifdef CONFIG_CHIPSET_RESET_HOOK
static void lpc_chipset_reset(void)
{
	hook_notify(HOOK_CHIPSET_RESET);
}
DECLARE_DEFERRED(lpc_chipset_reset);
#endif

int lpc_get_pltrst_asserted(void)
{
	/* Read current PLTRST status */
	return IS_BIT_SET(NPCX_MSWCTL1, NPCX_MSWCTL1_PLTRST_ACT);
}

#ifndef CONFIG_HOST_INTERFACE_ESPI
/* Initialize host settings by interrupt */
void lpc_lreset_pltrst_handler(void)
{
	int pltrst_asserted;

	/* Clear pending bit of WUI */
	SET_BIT(NPCX_WKPCL(MIWU_TABLE_0, MIWU_GROUP_5), 7);

	/* Ignore PLTRST# from SOC if it is not valid */
	if (chipset_pltrst_is_valid && !chipset_pltrst_is_valid())
		return;

	pltrst_asserted = lpc_get_pltrst_asserted();

	CPRINTS("LPC RESET# %sasserted", pltrst_asserted ? "" : "de");

	/*
	 * Once LRESET is de-asserted (low -> high), we need to initialize lpc
	 * settings once. If RSTCTL_LRESET_PLTRST_MODE is active, LPC registers
	 * won't be reset by Host domain reset but Core domain does.
	 */
	if (!pltrst_asserted)
		host_register_init();
	else {
		/* Clear processing flag when LRESET is asserted */
		CLEAR_BIT(NPCX_HIPMST(PMC_HOST_CMD), NPCX_HIPMST_F0);
#ifdef CONFIG_CHIPSET_RESET_HOOK
		/* Notify HOOK_CHIPSET_RESET */
		hook_call_deferred(&lpc_chipset_reset_data, MSEC);
#endif
	}
}
#endif

/*****************************************************************************/
/* LPC/eSPI Initialization functions */

static void lpc_init(void)
{
	/* Enable clock for LPC peripheral */
	clock_enable_peripheral(CGC_OFFSET_LPC, CGC_LPC_MASK,
				CGC_MODE_RUN | CGC_MODE_SLEEP);
	/*
	 * In npcx5/7, the host interface type (HIF_TYP_SEL in the DEVCNT
	 * register) is updated by booter after VCC1 Power-Up reset according to
	 * VHIF voltage.
	 * In npcx9, the booter will not do this anymore. The HIF_TYP_SEL
	 * field should be set by firmware.
	 */
#ifdef CONFIG_HOST_INTERFACE_ESPI
	/* Initialize eSPI module */
	NPCX_DEVCNT |= 0x08;
	espi_init();
#else
	/* Switching to LPC interface */
	NPCX_DEVCNT |= 0x04;
#endif
	/* Enable 4E/4F */
	if (!IS_BIT_SET(NPCX_MSWCTL1, NPCX_MSWCTL1_VHCFGA)) {
		NPCX_HCBAL = 0x4E;
		NPCX_HCBAH = 0x0;
	}
	/* Clear Host Access Hold state */
	NPCX_SMC_CTL = 0xC0;

#ifndef CONFIG_HOST_INTERFACE_ESPI
	/*
	 * Set alternative pin from GPIO to CLKRUN no matter SERIRQ is under
	 * continuous or quiet mode.
	 */
	SET_BIT(NPCX_DEVALT(1), NPCX_DEVALT1_CLKRN_SL);
#endif

	/*
	 * Set pin-mux from GPIOs to SCL/SMI to make sure toggling SCIB/SMIB is
	 * valid if CONFIG_SCI_GPIO isn't defined. eSPI sends SMI/SCI through VW
	 * automatically by toggling them, too. It's unnecessary to set pin mux.
	 */
#if !defined(CONFIG_SCI_GPIO) && !defined(CONFIG_HOST_INTERFACE_ESPI)
	SET_BIT(NPCX_DEVALT(1), NPCX_DEVALT1_EC_SCI_SL);
	SET_BIT(NPCX_DEVALT(1), NPCX_DEVALT1_SMI_SL);
#endif

	/* Initialize Hardware for UART Host */
#ifdef CONFIG_UART_HOST
	/* Init COMx LPC UART */
	/* FMCLK have to using 50MHz */
	NPCX_DEVALT(0xB) = 0xFF;
	/* Make sure Host Access unlock */
	CLEAR_BIT(NPCX_LKSIOHA, 2);
	/* Clear Host Access Lock Violation */
	SET_BIT(NPCX_SIOLV, 2);
#endif

	/* Don't stall SHM transactions */
	NPCX_SHM_CTL = NPCX_SHM_CTL & ~0x40;
	/* Disable Protect Win1&2*/
	NPCX_WIN_WR_PROT(0) = 0;
	NPCX_WIN_WR_PROT(1) = 0;
	NPCX_WIN_RD_PROT(0) = 0;
	NPCX_WIN_RD_PROT(1) = 0;
	/* Open Win1 256 byte for Host CMD, Win2 256 for MEMMAP*/
	NPCX_WIN_SIZE = 0x88;
	NPCX_WIN_BASE(0) = (uint32_t)shm_mem_host_cmd;
	NPCX_WIN_BASE(1) = (uint32_t)shm_memmap;
	/* Write protect of Share memory */
	NPCX_WIN_WR_PROT(1) = 0xFF;

	/* We support LPC args and version 3 protocol */
	*(lpc_get_memmap_range() + EC_MEMMAP_HOST_CMD_FLAGS) =
		EC_HOST_CMD_FLAG_LPC_ARGS_SUPPORTED |
		EC_HOST_CMD_FLAG_VERSION_3;

	/*
	 * Clear processing flag before enabling lpc's interrupts in case
	 * it's set by the other command during sysjump.
	 */
	CLEAR_BIT(NPCX_HIPMST(PMC_HOST_CMD), NPCX_HIPMST_F0);

	/* Turn on PMC2 for Host Command usage */
	SET_BIT(NPCX_HIPMCTL(PMC_HOST_CMD), 0);

	/*
	 * Set required control value (avoid setting HOSTWAIT bit at this stage)
	 */
	NPCX_SMC_CTL = NPCX_SMC_CTL & ~0x7F;
	/* Clear status */
	NPCX_SMC_STS = NPCX_SMC_STS;

	/* Create mailbox */

	/*
	 * Init KBC
	 * Clear OBF status flag,
	 * IBF(K&M) INT enable,
	 * OBF Mouse Full INT enable and OBF KB Full INT enable
	 */
#ifdef HAS_TASK_KEYPROTO
	lpc_keyboard_clear_buffer();
	NPCX_HICTRL = 0x0B;
#endif

	/*
	 * Turn on enhance mode on PM channel-1,
	 * enable IBF core interrupt
	 */
	NPCX_HIPMCTL(PMC_ACPI) |= 0x81;
#ifdef CONFIG_NCPX_KBC_IRQ_ACTIVE_LOW
	/* Inverted polarity on IRQ1 and IRQ12 (level + low) */
	NPCX_HIIRQC = 0x40;
#else
	/* Normal polarity on IRQ1 and IRQ12 (level + high) */
	NPCX_HIIRQC = 0x00;
#endif

	/*
	 * Init PORT80
	 * Enable Port80, Enable Port80 function & Interrupt & Read auto
	 */
#ifdef CONFIG_HOST_INTERFACE_ESPI
	NPCX_DP80CTL = 0x2b;
#else
	NPCX_DP80CTL = 0x29;
#endif
	SET_BIT(NPCX_GLUE_SDP_CTS, 3);
#if SUPPORT_P80_SEG
	SET_BIT(NPCX_GLUE_SDP_CTS, 0);
#endif

	/*
	 * Use SMI/SCI postive polarity as default.
	 * Negative polarity must be enabled in the case that SMI/SCI is
	 * generated automatically by hardware. In current design,
	 * SMI/SCI is conntrolled by FW. Use postive polarity is more
	 * intuitive.
	 */
	CLEAR_BIT(NPCX_HIPMCTL(PMC_ACPI), NPCX_HIPMCTL_SCIPOL);
	CLEAR_BIT(NPCX_HIPMIC(PMC_ACPI), NPCX_HIPMIC_SMIPOL);
	/* Set SMIB/SCIB to make sure SMI/SCI are high at init */
	NPCX_HIPMIC(PMC_ACPI) = NPCX_HIPMIC(PMC_ACPI) | BIT(NPCX_HIPMIC_SMIB) |
				BIT(NPCX_HIPMIC_SCIB);
#ifndef CONFIG_SCI_GPIO
	/*
	 * Allow SMI/SCI generated from PM module.
	 * Either hardware autimatically generates,
	 * or set SCIB/SMIB bit in HIPMIC register.
	 */
	SET_BIT(NPCX_HIPMIE(PMC_ACPI), NPCX_HIPMIE_SCIE);
	SET_BIT(NPCX_HIPMIE(PMC_ACPI), NPCX_HIPMIE_SMIE);
#endif
	lpc_task_enable_irq();

	/* Sufficiently initialized */
	init_done = 1;

	/* Update host events now that we can copy them to memmap */
	lpc_update_host_event_status();

	/*
	 * TODO: For testing LPC with Chromebox, please make sure LPC_CLK is
	 * generated before executing this function. EC needs LPC_CLK to access
	 * LPC register through SIB module. For Chromebook platform, this
	 * functionality should be done by BIOS or executed in hook function of
	 * HOOK_CHIPSET_STARTUP
	 */
#ifdef BOARD_NPCX_EVB
	/* initial IO port address via SIB-write modules */
	host_register_init();
#else
#ifndef CONFIG_HOST_INTERFACE_ESPI
	/*
	 * Initialize LRESET# interrupt only in case of LPC. For eSPI, there is
	 * no dedicated GPIO pin for LRESET/PLTRST. PLTRST is indicated as a VW
	 * signal instead. WUI57 of table 0 is set when EC receives
	 * LRESET/PLTRST from either VW or GPIO. Since WUI57 of table 0 and
	 * WUI15 of table 2 are issued at the same time in case of eSPI, there
	 * is no need to indicate LRESET/PLTRST via two sources. Thus, do not
	 * initialize LRESET# interrupt in case of eSPI.
	 */
	/* Set detection mode to edge */
	CLEAR_BIT(NPCX_WKMOD(MIWU_TABLE_0, MIWU_GROUP_5), 7);
	/* Handle interrupting on any edge */
	SET_BIT(NPCX_WKAEDG(MIWU_TABLE_0, MIWU_GROUP_5), 7);
	/* Enable wake-up input sources */
	SET_BIT(NPCX_WKEN(MIWU_TABLE_0, MIWU_GROUP_5), 7);
#endif
#endif
}
/*
 * Set prio to higher than default; this way LPC memory mapped data is ready
 * before other inits try to initialize their memmap data.
 */
DECLARE_HOOK(HOOK_INIT, lpc_init, HOOK_PRIO_INIT_LPC);

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

#if DEBUG_LPC
static int command_lpc(int argc, const char **argv)
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
DECLARE_CONSOLE_COMMAND(lpc, command_lpc, "[sci|smi|wake]", "Trigger SCI/SMI");

#endif
