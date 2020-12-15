/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <atomic.h>
#include <device.h>
#include <drivers/espi.h>
#include <logging/log.h>
#include <kernel.h>
#include <stdint.h>
#include <zephyr.h>

#include "chipset.h"
#include "common.h"
#include "espi.h"
#include "lpc.h"
#include "port80.h"
#include "zephyr_espi_shim.h"

LOG_MODULE_REGISTER(espi_shim, CONFIG_ESPI_LOG_LEVEL);

/*
 * A mapping of platform/ec signals to Zephyr virtual wires.
 *
 * This should be a macro which takes a parameter M, and does a
 * functional application of M to 2-tuples of (platform/ec signal,
 * zephyr vwire).
 */
#define VW_SIGNAL_TRANSLATION_LIST(M)                                 \
	M(VW_SLP_S3_L, ESPI_VWIRE_SIGNAL_SLP_S3)                      \
	M(VW_SLP_S4_L, ESPI_VWIRE_SIGNAL_SLP_S4)                      \
	M(VW_SLP_S5_L, ESPI_VWIRE_SIGNAL_SLP_S5)                      \
	M(VW_SUS_STAT_L, ESPI_VWIRE_SIGNAL_SUS_STAT)                  \
	M(VW_PLTRST_L, ESPI_VWIRE_SIGNAL_PLTRST)                      \
	M(VW_OOB_RST_WARN, ESPI_VWIRE_SIGNAL_OOB_RST_WARN)            \
	M(VW_OOB_RST_ACK, ESPI_VWIRE_SIGNAL_OOB_RST_ACK)              \
	M(VW_WAKE_L, ESPI_VWIRE_SIGNAL_WAKE)                          \
	M(VW_PME_L, ESPI_VWIRE_SIGNAL_PME)                            \
	M(VW_ERROR_FATAL, ESPI_VWIRE_SIGNAL_ERR_FATAL)                \
	M(VW_ERROR_NON_FATAL, ESPI_VWIRE_SIGNAL_ERR_NON_FATAL)        \
	M(VW_SLAVE_BTLD_STATUS_DONE, ESPI_VWIRE_SIGNAL_SLV_BOOT_DONE) \
	M(VW_SCI_L, ESPI_VWIRE_SIGNAL_SCI)                            \
	M(VW_SMI_L, ESPI_VWIRE_SIGNAL_SMI)                            \
	M(VW_HOST_RST_ACK, ESPI_VWIRE_SIGNAL_HOST_RST_ACK)            \
	M(VW_HOST_RST_WARN, ESPI_VWIRE_SIGNAL_HOST_RST_WARN)          \
	M(VW_SUS_ACK, ESPI_VWIRE_SIGNAL_SUS_ACK)                      \
	M(VW_SUS_WARN_L, ESPI_VWIRE_SIGNAL_SUS_WARN)                  \
	M(VW_SUS_PWRDN_ACK_L, ESPI_VWIRE_SIGNAL_SUS_PWRDN_ACK)        \
	M(VW_SLP_A_L, ESPI_VWIRE_SIGNAL_SLP_A)                        \
	M(VW_SLP_LAN, ESPI_VWIRE_SIGNAL_SLP_LAN)                      \
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

static void espi_peripheral_handler(const struct device *dev,
				    struct espi_callback *cb,
				    struct espi_event event)
{
	uint16_t event_type = event.evt_details;

	if (IS_ENABLED(CONFIG_PLATFORM_EC_PORT80) &&
	    event_type == ESPI_PERIPHERAL_DEBUG_PORT80) {
		port_80_write(event.evt_data);
	}
}

#define ESPI_DEV DT_LABEL(DT_NODELABEL(espi0))
static const struct device *espi_dev;

int zephyr_shim_setup_espi(void)
{
	static struct {
		struct espi_callback cb;
		espi_callback_handler_t handler;
		enum espi_bus_event event_type;
	} callbacks[] = {
		{
			.handler = espi_vwire_handler,
			.event_type = ESPI_BUS_EVENT_VWIRE_RECEIVED,
		},
		{
			.handler = espi_peripheral_handler,
			.event_type = ESPI_BUS_PERIPHERAL_NOTIFICATION,
		},
	};

	struct espi_cfg cfg = {
		.io_caps = ESPI_IO_MODE_SINGLE_LINE,
		.channel_caps = ESPI_CHANNEL_VWIRE | ESPI_CHANNEL_PERIPHERAL |
				ESPI_CHANNEL_OOB,
		.max_freq = 20,
	};

	espi_dev = device_get_binding(ESPI_DEV);
	if (!espi_dev) {
		LOG_ERR("Failed to find device %s", ESPI_DEV);
		return -1;
	}

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

int espi_vw_set_wire(enum espi_vw_signal signal, uint8_t level)
{
	return espi_send_vwire(espi_dev, signal_to_zephyr_vwire(signal), level);
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

static uint8_t lpc_memmap[256] __aligned(8);

uint8_t *lpc_get_memmap_range(void)
{
	/* TODO(b/175217186): implement eSPI functions for host commands */
	return lpc_memmap;
}

void lpc_update_host_event_status(void)
{
	/* TODO(b/175217186): implement eSPI functions for host commands */
}
