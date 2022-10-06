/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <atomic.h>
#include <zephyr/drivers/espi.h>
#include <x86_non_dsx_common_pwrseq_sm_handler.h>

#include "signal_vw.h"

#define MY_COMPAT intel_ap_pwrseq_vw

#if HAS_VW_SIGNALS

/*
 * A callback must be registered on the ESPI device (for the
 * bus events that are required to be handled) that calls
 * power_signal_espi_cb().
 *
 * This registration is done in a common ESPI initialisation module so
 * that there is no possibility of missing events.
 */

LOG_MODULE_DECLARE(ap_pwrseq, CONFIG_AP_PWRSEQ_LOG_LEVEL);

#define INIT_ESPI_SIGNAL(id)                                            \
	{                                                               \
		.espi_signal = DT_STRING_UPPER_TOKEN(id, virtual_wire), \
		.signal = PWR_SIGNAL_ENUM(id),                          \
		.invert = DT_PROP(id, vw_invert),                       \
	},

/*
 * Struct containing the eSPI virtual wire config.
 */
struct vw_config {
	uint8_t espi_signal; /* associated VW signal */
	uint8_t signal; /* power signal */
	bool invert; /* Invert the signal value */
};

const static struct vw_config vw_config[] = { DT_FOREACH_STATUS_OKAY(
	MY_COMPAT, INIT_ESPI_SIGNAL) };

/*
 * Current signal value.
 */
static atomic_t signal_data;
/*
 * Mask of valid signals. A signal is considered valid once an
 * initial value has been received for it.
 */
static atomic_t signal_valid;

#define espi_dev DEVICE_DT_GET(DT_CHOSEN(intel_ap_pwrseq_espi))

BUILD_ASSERT(ARRAY_SIZE(vw_config) <= (sizeof(atomic_t) * 8));

/*
 * Set the value of the VW signal, and optionally
 * call the power signal interrupt handling.
 */
static void vw_set(int index, int data, bool notify)
{
	bool value = vw_config[index].invert ? !data : !!data;

	atomic_set_bit_to(&signal_data, index, value);
	atomic_set_bit(&signal_valid, index);
	if (notify) {
		power_signal_interrupt(vw_config[index].signal, value);
	}
}

/*
 * Update all the VW signals.
 */
static void vw_update_all(bool notify)
{
	for (int i = 0; i < ARRAY_SIZE(vw_config); i++) {
		uint8_t vw_value;

		if (espi_receive_vwire(espi_dev, vw_config[i].espi_signal,
				       &vw_value) == 0) {
			vw_set(i, vw_value, notify);
		}
	}
}

void power_signal_espi_cb(const struct device *dev, struct espi_callback *cb,
			  struct espi_event event)
{
	LOG_DBG("ESPI event type 0x%x %d:%d", event.evt_type, event.evt_details,
		event.evt_data);
	switch (event.evt_type) {
	default:
		__ASSERT(0, "ESPI unknown event type: %d", event.evt_type);
		break;

	case ESPI_BUS_EVENT_CHANNEL_READY:
		/* Virtual wire channel status change */
		if (event.evt_details == ESPI_CHANNEL_VWIRE) {
			if (event.evt_data) {
				/* If now ready, update all the signals */
				vw_update_all(true);
			} else {
				/* If not ready, invalidate the signals */
				atomic_clear(&signal_valid);
			}
		}
		break;

	case ESPI_BUS_EVENT_VWIRE_RECEIVED:
		for (int i = 0; i < ARRAY_SIZE(vw_config); i++) {
			if (event.evt_details == vw_config[i].espi_signal) {
				vw_set(i, event.evt_data, true);
			}
		}
		break;
	}
}

int power_signal_vw_get(enum pwr_sig_vw vw)
{
	if (vw < 0 || vw >= ARRAY_SIZE(vw_config) ||
	    !atomic_test_bit(&signal_valid, vw)) {
		return -EINVAL;
	}
	return atomic_test_bit(&signal_data, vw);
}

void power_signal_vw_init(void)
{
	/*
	 * Check whether the bus is ready, and if so,
	 * initialise the current values of the signals.
	 */
	if (espi_get_channel_status(espi_dev, ESPI_CHANNEL_VWIRE)) {
		vw_update_all(false);
	}
}

#endif /* HAS_VW_SIGNALS */
