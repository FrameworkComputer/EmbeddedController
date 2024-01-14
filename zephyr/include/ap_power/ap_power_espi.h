/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief API for power signal ESPI callback.
 */

#ifndef __AP_POWER_AP_POWER_ESPI_H__
#define __AP_POWER_AP_POWER_ESPI_H__

#include <zephyr/drivers/espi.h>

/**
 * @brief ESPI callback for power signal handling.
 *
 * This callback must be registered for the bus events indicated below
 * as part of the common ESPI initialisation and configuration.
 *
 * @param dev ESPI device
 * @param cb Callback structure
 * @param event ESPI event data
 */
void power_signal_espi_cb(const struct device *dev, struct espi_callback *cb,
			  struct espi_event event);

/*
 * The ESPI bus events required for the power signal ESPI callback.
 */
#define POWER_SIGNAL_ESPI_BUS_EVENTS                                    \
	(ESPI_BUS_EVENT_CHANNEL_READY | ESPI_BUS_EVENT_VWIRE_RECEIVED | \
	 ESPI_BUS_RESET)

#endif /* __AP_POWER_AP_POWER_ESPI_H__ */
