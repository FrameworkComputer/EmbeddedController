/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <atomic.h>
#include <device.h>
#include <drivers/espi.h>
#include <drivers/gpio.h>
#include <logging/log.h>
#include <kernel.h>
#include <stdint.h>
#include <zephyr.h>

#include <ap_power/ap_power.h>
#include <ap_power/ap_power_events.h>
#include "acpi.h"
#include "chipset.h"
#include "common.h"
#include "espi.h"
#include "hooks.h"
#include "i8042_protocol.h"
#include "keyboard_protocol.h"
#include "lpc.h"
#include "port80.h"
#include "power.h"
#include "task.h"
#include "timer.h"
#include "zephyr_espi_shim.h"

#define VWIRE_PULSE_TRIGGER_TIME 65

LOG_MODULE_REGISTER(espi_shim, CONFIG_ESPI_LOG_LEVEL);

/* host command packet handler structure */
static struct host_packet lpc_packet;
/*
 * For the eSPI host command, request & response use the same share memory.
 * This is for input request temp buffer.
 */
static uint8_t params_copy[EC_LPC_HOST_PACKET_SIZE] __aligned(4);
static bool init_done;

/*
 * A mapping of platform/ec signals to Zephyr virtual wires.
 *
 * This should be a macro which takes a parameter M, and does a
 * functional application of M to 2-tuples of (platform/ec signal,
 * zephyr vwire).
 */
#define VW_SIGNAL_TRANSLATION_LIST(M)                                      \
	M(VW_SLP_S3_L, ESPI_VWIRE_SIGNAL_SLP_S3)                           \
	M(VW_SLP_S4_L, ESPI_VWIRE_SIGNAL_SLP_S4)                           \
	M(VW_SLP_S5_L, ESPI_VWIRE_SIGNAL_SLP_S5)                           \
	M(VW_SUS_STAT_L, ESPI_VWIRE_SIGNAL_SUS_STAT)                       \
	M(VW_PLTRST_L, ESPI_VWIRE_SIGNAL_PLTRST)                           \
	M(VW_OOB_RST_WARN, ESPI_VWIRE_SIGNAL_OOB_RST_WARN)                 \
	M(VW_OOB_RST_ACK, ESPI_VWIRE_SIGNAL_OOB_RST_ACK)                   \
	M(VW_WAKE_L, ESPI_VWIRE_SIGNAL_WAKE)                               \
	M(VW_PME_L, ESPI_VWIRE_SIGNAL_PME)                                 \
	M(VW_ERROR_FATAL, ESPI_VWIRE_SIGNAL_ERR_FATAL)                     \
	M(VW_ERROR_NON_FATAL, ESPI_VWIRE_SIGNAL_ERR_NON_FATAL)             \
	M(VW_PERIPHERAL_BTLD_STATUS_DONE, ESPI_VWIRE_SIGNAL_SLV_BOOT_DONE) \
	M(VW_SCI_L, ESPI_VWIRE_SIGNAL_SCI)                                 \
	M(VW_SMI_L, ESPI_VWIRE_SIGNAL_SMI)                                 \
	M(VW_HOST_RST_ACK, ESPI_VWIRE_SIGNAL_HOST_RST_ACK)                 \
	M(VW_HOST_RST_WARN, ESPI_VWIRE_SIGNAL_HOST_RST_WARN)               \
	M(VW_SUS_ACK, ESPI_VWIRE_SIGNAL_SUS_ACK)                           \
	M(VW_SUS_WARN_L, ESPI_VWIRE_SIGNAL_SUS_WARN)                       \
	M(VW_SUS_PWRDN_ACK_L, ESPI_VWIRE_SIGNAL_SUS_PWRDN_ACK)             \
	M(VW_SLP_A_L, ESPI_VWIRE_SIGNAL_SLP_A)                             \
	M(VW_SLP_LAN, ESPI_VWIRE_SIGNAL_SLP_LAN)                           \
	M(VW_SLP_WLAN, ESPI_VWIRE_SIGNAL_SLP_WLAN)

/*
 * These two macros are intended to be used as as the M parameter to
 * the list above, generating case statements returning the
 * translation for the first parameter to the second, and the second
 * to the first, respectively.
 */
#define CASE_CROS_TO_ZEPHYR(A, B) \
	case A:                   \
		return B;
#define CASE_ZEPHYR_TO_CROS(A, B) CASE_CROS_TO_ZEPHYR(B, A)

#if !defined(CONFIG_AP_PWRSEQ)
/* Translate a platform/ec signal to a Zephyr signal */
static enum espi_vwire_signal signal_to_zephyr_vwire(enum espi_vw_signal signal)
{
	switch (signal) {
		VW_SIGNAL_TRANSLATION_LIST(CASE_CROS_TO_ZEPHYR);
	default:
		LOG_ERR("Invalid virtual wire signal (%d)", signal);
		return -1;
	}
}

/* Translate a Zephyr vwire to a platform/ec signal */
static enum espi_vw_signal zephyr_vwire_to_signal(enum espi_vwire_signal vwire)
{
	switch (vwire) {
		VW_SIGNAL_TRANSLATION_LIST(CASE_ZEPHYR_TO_CROS);
	default:
		LOG_ERR("Invalid zephyr vwire (%d)", vwire);
		return -1;
	}
}

/*
 * Bit field for each signal which can have an interrupt enabled.
 * Note the interrupt is always enabled, it just depends whether we
 * route it to the power_signal_interrupt handler or not.
 */
static atomic_t signal_interrupt_enabled;

/* To be used with VW_SIGNAL_TRASLATION_LIST */
#define CASE_CROS_TO_BIT(A, _) CASE_CROS_TO_ZEPHYR(A, BIT(A - VW_SIGNAL_START))

/* Convert from an EC signal to the corresponding interrupt enabled bit. */
static uint32_t signal_to_interrupt_bit(enum espi_vw_signal signal)
{
	switch (signal) {
		VW_SIGNAL_TRANSLATION_LIST(CASE_CROS_TO_BIT);
	default:
		return 0;
	}
}

/* Callback for vwire received */
static void espi_vwire_handler(const struct device *dev,
			       struct espi_callback *cb,
			       struct espi_event event)
{
	int ec_signal = zephyr_vwire_to_signal(event.evt_details);

	if (IS_ENABLED(CONFIG_PLATFORM_EC_POWERSEQ) &&
	    (signal_interrupt_enabled & signal_to_interrupt_bit(ec_signal))) {
		power_signal_interrupt(ec_signal);
	}
}

#endif /* !defined(CONFIG_AP_PWRSEQ) */

#ifdef CONFIG_PLATFORM_EC_CHIPSET_RESET_HOOK
static void espi_chipset_reset(void)
{
	if (IS_ENABLED(CONFIG_AP_PWRSEQ)) {
		ap_power_ev_send_callbacks(AP_POWER_RESET);
	} else {
		hook_notify(HOOK_CHIPSET_RESET);
	}
}
DECLARE_DEFERRED(espi_chipset_reset);

/* Callback for reset */
static void espi_reset_handler(const struct device *dev,
			       struct espi_callback *cb,
			       struct espi_event event)
{
	hook_call_deferred(&espi_chipset_reset_data, MSEC);

}
#endif /* CONFIG_PLATFORM_EC_CHIPSET_RESET_HOOK */

#define espi_dev DEVICE_DT_GET(DT_CHOSEN(cros_ec_espi))

#if !defined(CONFIG_AP_PWRSEQ)

int espi_vw_set_wire(enum espi_vw_signal signal, uint8_t level)
{
	int ret = espi_send_vwire(espi_dev, signal_to_zephyr_vwire(signal),
				  level);

	if (ret != 0)
		LOG_ERR("Encountered error sending virtual wire signal");

	return ret;
}

int espi_vw_get_wire(enum espi_vw_signal signal)
{
	uint8_t level;

	if (espi_receive_vwire(espi_dev, signal_to_zephyr_vwire(signal),
			       &level) < 0) {
		LOG_ERR("Encountered error receiving virtual wire signal");
		return 0;
	}

	return level;
}

int espi_vw_enable_wire_int(enum espi_vw_signal signal)
{
	atomic_or(&signal_interrupt_enabled, signal_to_interrupt_bit(signal));
	return 0;
}

int espi_vw_disable_wire_int(enum espi_vw_signal signal)
{
	atomic_and(&signal_interrupt_enabled, ~signal_to_interrupt_bit(signal));
	return 0;
}

#endif /* !defined(CONFIG_AP_PWRSEQ) */

uint8_t *lpc_get_memmap_range(void)
{
	uint32_t lpc_memmap = 0;
	int result = espi_read_lpc_request(espi_dev, EACPI_GET_SHARED_MEMORY,
					   &lpc_memmap);

	if (result != EC_SUCCESS)
		LOG_ERR("Get lpc_memmap failed (%d)!\n", result);

	return (uint8_t *)lpc_memmap;
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
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_pch_wake_odl),
			!wake_events);
}

#if !defined(CONFIG_AP_PWRSEQ)

static void lpc_generate_smi(void)
{
	/* Enforce signal-high for long enough to debounce high */
	espi_vw_set_wire(VW_SMI_L, 1);
	udelay(VWIRE_PULSE_TRIGGER_TIME);
	espi_vw_set_wire(VW_SMI_L, 0);
	udelay(VWIRE_PULSE_TRIGGER_TIME);
	espi_vw_set_wire(VW_SMI_L, 1);
}

static void lpc_generate_sci(void)
{
	/* Enforce signal-high for long enough to debounce high */
	espi_vw_set_wire(VW_SCI_L, 1);
	udelay(VWIRE_PULSE_TRIGGER_TIME);
	espi_vw_set_wire(VW_SCI_L, 0);
	udelay(VWIRE_PULSE_TRIGGER_TIME);
	espi_vw_set_wire(VW_SCI_L, 1);
}

#else

/*
 * Use Zephyr API.
 */
static void lpc_generate_signal(enum espi_vwire_signal signal)
{
	/* Enforce signal-high for long enough to debounce high */
	espi_send_vwire(espi_dev, signal, 1);
	udelay(VWIRE_PULSE_TRIGGER_TIME);
	espi_send_vwire(espi_dev, signal, 0);
	udelay(VWIRE_PULSE_TRIGGER_TIME);
	espi_send_vwire(espi_dev, signal, 1);
}

static void lpc_generate_sci(void)
{
	lpc_generate_signal(ESPI_VWIRE_SIGNAL_SCI);
}

static void lpc_generate_smi(void)
{
	lpc_generate_signal(ESPI_VWIRE_SIGNAL_SMI);
}

#endif /* !defined(CONFIG_AP_PWRSEQ) */

void lpc_update_host_event_status(void)
{
	uint32_t enable;
	uint32_t status;
	int need_sci = 0;
	int need_smi = 0;

	if (!init_done)
		return;

	/* Disable PMC1 interrupt while updating status register */
	enable = 0;
	espi_write_lpc_request(espi_dev, ECUSTOM_HOST_SUBS_INTERRUPT_EN,
			       &enable);

	espi_read_lpc_request(espi_dev, EACPI_READ_STS, &status);
	if (lpc_get_host_events_by_type(LPC_HOST_EVENT_SMI)) {
		/* Only generate SMI for first event */
		if (!(status & EC_LPC_STATUS_SMI_PENDING))
			need_smi = 1;

		status |= EC_LPC_STATUS_SMI_PENDING;
		espi_write_lpc_request(espi_dev, EACPI_WRITE_STS, &status);
	} else {
		status &= ~EC_LPC_STATUS_SMI_PENDING;
		espi_write_lpc_request(espi_dev, EACPI_WRITE_STS, &status);
	}

	espi_read_lpc_request(espi_dev, EACPI_READ_STS, &status);
	if (lpc_get_host_events_by_type(LPC_HOST_EVENT_SCI)) {
		/* Generate SCI for every event */
		need_sci = 1;

		status |= EC_LPC_STATUS_SCI_PENDING;
		espi_write_lpc_request(espi_dev, EACPI_WRITE_STS, &status);
	} else {
		status &= ~EC_LPC_STATUS_SCI_PENDING;
		espi_write_lpc_request(espi_dev, EACPI_WRITE_STS, &status);
	}

	*(host_event_t *)host_get_memmap(EC_MEMMAP_HOST_EVENTS) =
		lpc_get_host_events();

	enable = 1;
	espi_write_lpc_request(espi_dev, ECUSTOM_HOST_SUBS_INTERRUPT_EN,
			       &enable);

	/* Process the wake events. */
	lpc_update_wake(lpc_get_host_events_by_type(LPC_HOST_EVENT_WAKE));

	/* Send pulse on SMI signal if needed */
	if (need_smi)
		lpc_generate_smi();

	/* ACPI 5.0-12.6.1: Generate SCI for SCI_EVT=1. */
	if (need_sci)
		lpc_generate_sci();
}

static void host_command_init(void)
{
	/* We support LPC args and version 3 protocol */
	*(lpc_get_memmap_range() + EC_MEMMAP_HOST_CMD_FLAGS) =
		EC_HOST_CMD_FLAG_LPC_ARGS_SUPPORTED |
		EC_HOST_CMD_FLAG_VERSION_3;

	/* Sufficiently initialized */
	init_done = 1;

	lpc_update_host_event_status();
}

DECLARE_HOOK(HOOK_INIT, host_command_init, HOOK_PRIO_INIT_LPC);

static void handle_acpi_write(uint32_t data)
{
	uint8_t value, result;
	uint8_t is_cmd = is_acpi_command(data);
	uint32_t status;

	value = get_acpi_value(data);

	/* Handle whatever this was. */
	if (acpi_ap_to_ec(is_cmd, value, &result)) {
		data = result;
		espi_write_lpc_request(espi_dev, EACPI_WRITE_CHAR, &data);
	}

	/* Clear processing flag */
	espi_read_lpc_request(espi_dev, EACPI_READ_STS, &status);
	status &= ~EC_LPC_STATUS_PROCESSING;
	espi_write_lpc_request(espi_dev, EACPI_WRITE_STS, &status);

	/*
	 * ACPI 5.0-12.6.1: Generate SCI for Input Buffer Empty / Output Buffer
	 * Full condition on the kernel channel.
	 */
	lpc_generate_sci();
}

static void lpc_send_response_packet(struct host_packet *pkt)
{
	uint32_t data;

	/* TODO(b/176523211): check whether add EC_RES_IN_PROGRESS handle */

	/* Write result to the data byte.  This sets the TOH status bit. */
	data = pkt->driver_result;
	espi_write_lpc_request(espi_dev, ECUSTOM_HOST_CMD_SEND_RESULT, &data);
}

static void handle_host_write(uint32_t data)
{
	uint32_t shm_mem_host_cmd;

	if (EC_COMMAND_PROTOCOL_3 != (data & 0xff)) {
		LOG_ERR("Don't support this version of the host command");
		/* TODO:(b/175217186): error response for other versions */
		return;
	}

	espi_read_lpc_request(espi_dev, ECUSTOM_HOST_CMD_GET_PARAM_MEMORY,
			      &shm_mem_host_cmd);

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
}

void lpc_set_acpi_status_mask(uint8_t mask)
{
	uint32_t status;
	espi_read_lpc_request(espi_dev, EACPI_READ_STS, &status);
	status |= mask;
	espi_write_lpc_request(espi_dev, EACPI_WRITE_STS, &status);
}

void lpc_clear_acpi_status_mask(uint8_t mask)
{
	uint32_t status;
	espi_read_lpc_request(espi_dev, EACPI_READ_STS, &status);
	status &= ~mask;
	espi_write_lpc_request(espi_dev, EACPI_WRITE_STS, &status);
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

/*
 * This function is needed only for the obsolete platform which uses the GPIO
 * for KBC's IRQ.
 */
void lpc_keyboard_resume_irq(void) {}

void lpc_keyboard_clear_buffer(void)
{
	/* Clear OBF flag in host STATUS and HIKMST regs */
	espi_write_lpc_request(espi_dev, E8042_CLEAR_OBF, 0);
}
int lpc_keyboard_has_char(void)
{
	uint32_t status;

	/* if OBF bit is '1', that mean still have a data in DBBOUT */
	espi_read_lpc_request(espi_dev, E8042_OBF_HAS_CHAR, &status);
	return status;
}

void lpc_keyboard_put_char(uint8_t chr, int send_irq)
{
	uint32_t kb_char = chr;

	espi_write_lpc_request(espi_dev, E8042_WRITE_KB_CHAR, &kb_char);
	LOG_INF("KB put %02x", kb_char);
}

/* Put an aux char to host buffer by HIMDO and assert status bit 5. */
void lpc_aux_put_char(uint8_t chr, int send_irq)
{
	uint32_t kb_char = chr;
	uint32_t status = I8042_AUX_DATA;

	espi_write_lpc_request(espi_dev, E8042_SET_FLAG, &status);
	espi_write_lpc_request(espi_dev, E8042_WRITE_KB_CHAR, &kb_char);
	LOG_INF("AUX put %02x", kb_char);
}

static void kbc_ibf_obe_handler(uint32_t data)
{
#ifdef HAS_TASK_KEYPROTO
	uint8_t is_ibf = is_8042_ibf(data);
	uint32_t status = I8042_AUX_DATA;

	if (is_ibf) {
		keyboard_host_write(get_8042_data(data),
				    get_8042_type(data));
	} else if (IS_ENABLED(CONFIG_8042_AUX)) {
		espi_write_lpc_request(espi_dev, E8042_CLEAR_FLAG, &status);
	}
	task_wake(TASK_ID_KEYPROTO);
#endif
}

int lpc_keyboard_input_pending(void)
{
	uint32_t status;

	/* if IBF bit is '1', that mean still have a data in DBBIN */
	espi_read_lpc_request(espi_dev, E8042_IBF_HAS_CHAR, &status);
	return status;
}

static void espi_peripheral_handler(const struct device *dev,
				    struct espi_callback *cb,
				    struct espi_event event)
{
	uint16_t event_type = event.evt_details;

	if (IS_ENABLED(CONFIG_PLATFORM_EC_PORT80) &&
	    event_type == ESPI_PERIPHERAL_DEBUG_PORT80) {
		port_80_write(event.evt_data);
	}

	if (IS_ENABLED(CONFIG_PLATFORM_EC_ACPI) &&
	    event_type == ESPI_PERIPHERAL_HOST_IO) {
		handle_acpi_write(event.evt_data);
	}

	if (IS_ENABLED(CONFIG_PLATFORM_EC_HOSTCMD) &&
	    event_type == ESPI_PERIPHERAL_EC_HOST_CMD) {
		handle_host_write(event.evt_data);
	}

	if (IS_ENABLED(CONFIG_ESPI_PERIPHERAL_8042_KBC) &&
	    IS_ENABLED(HAS_TASK_KEYPROTO) &&
	    event_type == ESPI_PERIPHERAL_8042_KBC) {
		kbc_ibf_obe_handler(event.evt_data);
	}
}

static int zephyr_shim_setup_espi(const struct device *unused)
{
	static struct {
		struct espi_callback cb;
		espi_callback_handler_t handler;
		enum espi_bus_event event_type;
	} callbacks[] = {
#if !defined(CONFIG_AP_PWRSEQ)
		{
			.handler = espi_vwire_handler,
			.event_type = ESPI_BUS_EVENT_VWIRE_RECEIVED,
		},
#endif
		{
			.handler = espi_peripheral_handler,
			.event_type = ESPI_BUS_PERIPHERAL_NOTIFICATION,
		},
#ifdef CONFIG_PLATFORM_EC_CHIPSET_RESET_HOOK
		{
			.handler = espi_reset_handler,
			.event_type = ESPI_BUS_RESET,
		},
#endif
	};

	struct espi_cfg cfg = {
		.io_caps = ESPI_IO_MODE_QUAD_LINES,
		.channel_caps = ESPI_CHANNEL_VWIRE | ESPI_CHANNEL_PERIPHERAL |
				ESPI_CHANNEL_OOB,
		.max_freq = 50,
	};

	if (!device_is_ready(espi_dev))
		k_oops();

	/* Configure eSPI */
	if (espi_config(espi_dev, &cfg)) {
		LOG_ERR("Failed to configure eSPI device");
		return -1;
	}

	/* Setup callbacks */
	for (size_t i = 0; i < ARRAY_SIZE(callbacks); i++) {
		espi_init_callback(&callbacks[i].cb, callbacks[i].handler,
				   callbacks[i].event_type);
		espi_add_callback(espi_dev, &callbacks[i].cb);
	}

	return 0;
}

/* Must be before zephyr_shim_setup_hooks. */
SYS_INIT(zephyr_shim_setup_espi, APPLICATION, 0);

bool is_acpi_command(uint32_t data)
{
	struct espi_evt_data_acpi *acpi = (struct espi_evt_data_acpi *)&data;

	return acpi->type;
}

uint32_t get_acpi_value(uint32_t data)
{
	struct espi_evt_data_acpi *acpi = (struct espi_evt_data_acpi *)&data;

	return acpi->data;
}

bool is_8042_ibf(uint32_t data)
{
	struct espi_evt_data_kbc *kbc = (struct espi_evt_data_kbc *)&data;

	return kbc->evt & HOST_KBC_EVT_IBF;
}

bool is_8042_obe(uint32_t data)
{
	struct espi_evt_data_kbc *kbc = (struct espi_evt_data_kbc *)&data;

	return kbc->evt & HOST_KBC_EVT_OBE;
}

uint32_t get_8042_type(uint32_t data)
{
	struct espi_evt_data_kbc *kbc = (struct espi_evt_data_kbc *)&data;

	return kbc->type;
}

uint32_t get_8042_data(uint32_t data)
{
	struct espi_evt_data_kbc *kbc = (struct espi_evt_data_kbc *)&data;

	return kbc->data;
}
