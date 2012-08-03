/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery charging task and state machine.
 */

#include "battery.h"
#include "battery_pack.h"
#include "charge_state.h"
#include "charger.h"
#include "smart_battery.h"
#include "timer.h"
#include "uart.h"
#include "util.h"

/* Buffer size for charging resistance calculation */
#define LOG_BUFFER_SIZE  16

static int log_index;
static short log_volt[LOG_BUFFER_SIZE];
static short log_curr[LOG_BUFFER_SIZE];
static int baseline_voltage;
static int kicking_count;

static inline int time_after(timestamp_t now, timestamp_t orig, uint64_t usec)
{
	return (now.val > (orig.val + usec));
}

static inline void reset_data_log(void)
{
	log_index = 0;
}

static inline void trickle_charging_init(void)
{
	baseline_voltage = 0;
	kicking_count    = 0;
	reset_data_log();
}

/* Adjust charging voltage with voltage value range checking.
 * Reset data log and charger watchdog timer.
 */
static int set_voltage(struct power_state_context *ctx, int voltage)
{
	if (voltage <= ctx->curr.batt.desired_voltage && voltage > 0) {
		charger_set_voltage(voltage);
		charger_get_voltage(&ctx->curr.charging_voltage);
		ctx->charger_update_time = get_time();
		reset_data_log();
		return 0;
	}
	return -1;
}

/* Increase/decrease the charging voltage one step */
static int inc_voltage(struct power_state_context *ctx)
{
	return set_voltage(ctx, ctx->curr.charging_voltage +
			ctx->charger->voltage_step);
}

static int dec_voltage(struct power_state_context *ctx)
{
	return set_voltage(ctx, ctx->curr.charging_voltage -
			ctx->charger->voltage_step);
}

/* Bump up the charging voltage baseline to one step higher. */
static enum power_state go_next_level(struct power_state_context *ctx)
{
	if (inc_voltage(ctx))
		return PWR_STATE_ERROR;

	/* Battery chemical reaction lags behind the charging voltage
	 * change. Delay the charging state machine 2 seconds.
	 */
	usleep(SECOND * 2);
	charger_get_voltage(&baseline_voltage);

	return PWR_STATE_UNCHANGE;
}

/* Trickle charging handler
 *     - check trickle charging timeout
 *     - new state: INIT
 *     - exit condition: when desired_current reaches current_min
 *     - try to charge larger current when battery voltage reaches
 *       105% of voltage_min
 */
enum power_state trickle_charge(struct power_state_context *ctx)
{
	int sum_volt, sum_curr;
	int desired_volt, desired_curr;

	struct power_state_data *curr = &ctx->curr;
	struct batt_params *batt      = &curr->batt;
	const struct charger_info *cinfo    = ctx->charger;
	const struct battery_info *binfo    = ctx->battery;

	/* Clear trickle charging duration on AC change */
	if (curr->ac != ctx->prev.ac) {
		ctx->trickle_charging_time.val = 0;
		if (!curr->ac)
			return PWR_STATE_INIT;
	}

	/* Start timer */
	if (ctx->trickle_charging_time.val == 0) {
		trickle_charging_init();
		ctx->trickle_charging_time = get_time();
	}

	/* Check charger reset */
	if (curr->charging_voltage == 0 || curr->charging_current == 0) {
		ctx->trickle_charging_time.val = 0;
		return PWR_STATE_INIT;
	}

	/* 4 hours is *long* enough to pre-charge a large battery (8000mAh)
	 * using minimal current (5mAh).
	 */
	if (time_after(curr->ts, ctx->trickle_charging_time, HOUR * 4))
		return PWR_STATE_ERROR;

	if (curr->error & F_BATTERY_MASK)
		return PWR_STATE_UNCHANGE;

	/* End of pre-charge condition. Battery desired a current higher
	 * than the minimal charging cap.
	 */
	if (batt->desired_current > cinfo->current_min) {
		trickle_charging_init();
		ctx->trickle_charging_time.val = 0;
		return PWR_STATE_INIT;
	}

	/* If the trickle charging current drops to zero, raise charging
	 * voltage baseline to next level.
	 */
	if (batt->current == 0)
		return go_next_level(ctx);

	/* When the battery voltage reaches normal charging value (105% min),
	 * try kicking the current up and see if it starts normal charging.
	 */
	if (kicking_count < 5 &&
			batt->voltage > (binfo->voltage_min * 105 / 100)) {
		kicking_count++;
		charger_set_voltage(batt->desired_voltage);
		usleep(5 * SECOND);
		desired_curr = 0;
		battery_desired_current(&desired_curr);
		if (desired_curr >= cinfo->current_min) {
			/* Exit trickle charging state */
			trickle_charging_init();
			ctx->trickle_charging_time.val = 0;
			return PWR_STATE_INIT;
		}
		charger_set_voltage(curr->charging_voltage);
		ctx->charger_update_time = get_time();

		reset_data_log();
		return PWR_STATE_UNCHANGE;
	}

	/* Over current protection. Decrease charging voltage and baseline
	 * voltage.
	 */
	if (batt->current > binfo->precharge_current) {
		dec_voltage(ctx);
		if (baseline_voltage > ctx->curr.charging_voltage)
			baseline_voltage = ctx->curr.charging_voltage;
		usleep(SECOND);
		reset_data_log();
		return PWR_STATE_UNCHANGE;
	}

	/* Voltage and current data acquisition. */
	if (log_index < LOG_BUFFER_SIZE) {
		log_volt[log_index] = batt->voltage;
		log_curr[log_index] = batt->current;
		log_index++;
		return PWR_STATE_UNCHANGE;
	}

	sum_volt = 0;
	sum_curr = 0;
	for (log_index = 0; log_index < LOG_BUFFER_SIZE; log_index++) {
		sum_volt += log_volt[log_index];
		sum_curr += log_curr[log_index];
	}

	reset_data_log();

	/* Estimate desired_voltage:
	 * Although the target current to desired voltage function is a
	 * monotonic function. To simplify the calculation, following
	 * code use linear estimation when the current delta is small.
	 *
	 *   V_desired = I_target * ( avg(dV_batt) / avg(I_batt) ) +
	 *                          V_batt
	 */
	desired_volt = (1 + batt->desired_current) *
		(curr->charging_voltage * LOG_BUFFER_SIZE - sum_volt) /
		sum_curr + batt->voltage;

	if (desired_volt > baseline_voltage) {
		if (desired_volt > curr->charging_voltage) {
			inc_voltage(ctx);
			usleep(SECOND);
			return PWR_STATE_UNCHANGE;
		}

		if (desired_volt < (curr->charging_voltage -
				cinfo->voltage_step)) {
			dec_voltage(ctx);
			usleep(SECOND);
			return PWR_STATE_UNCHANGE;
		}
	}

	/* Update charger watchdog periodically */
	if (time_after(curr->ts, ctx->charger_update_time,
			CHARGER_UPDATE_PERIOD)) {
		charger_set_current(curr->charging_current);
		ctx->charger_update_time = get_time();
	}

	return PWR_STATE_UNCHANGE;
}

