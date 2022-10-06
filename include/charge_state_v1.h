/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "timer.h"

#ifndef __CROS_EC_CHARGE_STATE_V1_H
#define __CROS_EC_CHARGE_STATE_V1_H

/* Update period to prevent charger watchdog timeout */
#define CHARGER_UPDATE_PERIOD (SECOND * 10)

/* Power state error flags */
#define F_CHARGER_INIT BIT(0) /* Charger initialization */
#define F_CHARGER_VOLTAGE BIT(1) /* Charger maximum output voltage */
#define F_CHARGER_CURRENT BIT(2) /* Charger maximum output current */
#define F_BATTERY_VOLTAGE BIT(3) /* Battery voltage */
#define F_BATTERY_MODE BIT(8) /* Battery mode */
#define F_BATTERY_CAPACITY BIT(9) /* Battery capacity */
#define F_BATTERY_STATE_OF_CHARGE BIT(10) /* State of charge, percentage */
#define F_BATTERY_UNRESPONSIVE BIT(11) /* Battery not responding */
#define F_BATTERY_NOT_CONNECTED BIT(12) /* Battery not connected */
#define F_BATTERY_GET_PARAMS BIT(13) /* Any battery parameter bad */

#define F_BATTERY_MASK                                             \
	(F_BATTERY_VOLTAGE | F_BATTERY_MODE | F_BATTERY_CAPACITY | \
	 F_BATTERY_STATE_OF_CHARGE | F_BATTERY_UNRESPONSIVE |      \
	 F_BATTERY_NOT_CONNECTED | F_BATTERY_GET_PARAMS)
#define F_CHARGER_MASK (F_CHARGER_VOLTAGE | F_CHARGER_CURRENT | F_CHARGER_INIT)

/* Power state data
 * Status collection of charging state machine.
 */
struct charge_state_data {
	int ac;
	int charging_voltage;
	int charging_current;
	struct batt_params batt;
	enum charge_state state;
	uint32_t error;
	timestamp_t ts;
};

/* State context
 * The shared context for state handler. The context contains current and
 * previous state.
 */
struct charge_state_context {
	struct charge_state_data curr;
	struct charge_state_data prev;
	timestamp_t charge_state_updated_time;
	uint32_t *memmap_batt_volt;
	uint32_t *memmap_batt_rate;
	uint32_t *memmap_batt_cap;
	uint8_t *memmap_batt_flags;
	/* Charger and battery pack info */
	const struct charger_info *charger;
	const struct battery_info *battery;
	/* Charging timestamps */
	timestamp_t charger_update_time;
	timestamp_t trickle_charging_time;
	timestamp_t voltage_debounce_time;
	timestamp_t shutdown_warning_time;
	int battery_responsive;
};

#endif /* __CROS_EC_CHARGE_STATE_V1_H */
