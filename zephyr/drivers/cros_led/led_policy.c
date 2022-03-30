/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <device.h>
#include <init.h>
#include <ap_power/ap_power.h>

#include "battery.h"
#include "charge_state.h"
#include "hooks.h"
#include "led_common.h"

#include "led.h"
#include "led_gpio.h"
#include "led_pwm.h"

#include <logging/log.h>

LOG_MODULE_REGISTER(led, LOG_LEVEL_ERR);

#define COMPAT_POLICY	cros_ec_led_policy

/*
 * LED policy processing, configured using the cros_ec_led_policy
 * compat, mapping the inputs to the LED behaviour and actions.
 * These inputs comprise of the AP state, the charger state, and the
 * battery state.
 * Once a policy entry is matched, the action list is processed by
 * passing the colors to the LEDs.
 * If there are multiple sets of colors, a duration is used
 * with a timer to cycle through the sets of colors.
 */

/*
 * Duration is stored in tenths of a second.
 */
#define D_TICKS(d)	K_MSEC((d) * 100)

/*
 * Enum representing the LED drivers.
 */
enum led_driver_type {
	LED_DRIVER_GPIO,
	LED_DRIVER_PWM,
};

/*
 * LED policy configuration.
 *
 * The overall idea is that when changes occur (such as the
 * state of the AP changing), the input states will be matched against
 * the different policies, and the first matching policy will
 * be used - each policy references a LED action, which defines for that
 * LED driver what it should do.
 */

/*
 * LED policy input structure. The policy is the combination of the
 * AP state, charger state, and battery capacity. Any or all of these
 * can be ignored.
 * The actions are the sets of color/duration values used
 * to drive the LEDs (as a byte array).
 */
struct led_policy_entry {
	uint8_t ap_state;	/* AP state to match */
	uint8_t charger_state;	/* Charger state to match */
	uint8_t battery[2];	/* Battery percentage range to match */
	uint8_t action_size;	/* Size of action byte array */
	const uint8_t *actions;	/* Action byte array */
};

/*
 * Inputs for LED policy control.
 * These enums define the policy inputs used to
 * identify the policy to use (and thus the LED action
 * to take).
 */
enum led_ap_state {
	LED_AP_ANY,		/* Any state */
	LED_AP_SUSPENDED,	/* AP is suspended */
	LED_AP_RUNNING,		/* AP is running */
	LED_AP_POWER_OFF,	/* AP is powered off */
};

enum led_charger_state {
	LED_CHARGER_ANY,		/* Any state */
	LED_CHARGER_FULL,		/* Charger present, battery full */
	LED_CHARGER_CHARGING,		/* Charging */
	LED_CHARGER_DISCHARGING,	/* No charger connected */
	LED_CHARGER_IDLE,		/* External power connected in IDLE */
	LED_CHARGER_ERROR,		/* Charger fault */
};

/*
 * The current state of the AP, charger and battery charge.
 */
static uint8_t cpu_state = LED_AP_POWER_OFF;
static uint8_t charger_state = LED_CHARGER_DISCHARGING;
static uint8_t battery_state;

/*
 * Define macros to map node names to identifiers for
 * the policy nodes and child nodes.
 */
#define POLICY_CHECK_TAB(id)	DT_CAT(P_C_T_, id)
#define LED_ACTION(id)	DT_CAT(L_A_, id)

#define AP_NAME(nm)	DT_CAT(LED_AP_, nm)
#define CHG_NAME(nm)	DT_CAT(LED_CHARGER_, nm)

/*
 * Generate action byte arrays, one for each policy match node.
 */
#define GEN_ACTION(id)					\
static const uint8_t LED_ACTION(id)[] =			\
	DT_PROP(id, action)				\
;

#define GEN_ACTION_ARRAYS(id)			\
	DT_FOREACH_CHILD(id, GEN_ACTION)	\

DT_FOREACH_STATUS_OKAY(COMPAT_POLICY, GEN_ACTION_ARRAYS)

/*
 * Generate the policy arrays.
 */
#define GEN_CHECK_TAB_ENTRY(id)					\
{								\
	.ap_state = AP_NAME(DT_STRING_TOKEN(id, cpu)),		\
	.charger_state = CHG_NAME(DT_STRING_TOKEN(id, charger)), \
	.battery = DT_PROP(id, battery),			\
	.actions = LED_ACTION(id),				\
	.action_size = ARRAY_SIZE(LED_ACTION(id)),		\
},

#define GEN_CHECK_TABLE(id)					\
static const struct led_policy_entry POLICY_CHECK_TAB(id)[] = {	\
	DT_FOREACH_CHILD(id, GEN_CHECK_TAB_ENTRY)		\
};

DT_FOREACH_STATUS_OKAY(COMPAT_POLICY, GEN_CHECK_TABLE)

/*
 * The RO portion of the policy.
 */
struct led_policy {
	uint8_t step_size;	/* Number of colors + duration in each set */
	enum led_driver_type driver;
	uint8_t index;		/* Index within driver */
	enum ec_led_id id_enum;	/* For common LED API */
	uint8_t count;		/* The number of policy entries */
	const struct led_policy_entry *entries;
};

/*
 * Macro to generate the common LED API enum, if present.
 * If no led id enum, use 0xFF. This is used to call
 * led_auto_control_is_enabled() to check whether our LED
 * is auto-controlled.
 */
#define GEN_LED_ENUM(id)				\
	COND_CODE_1(DT_NODE_HAS_PROP(id, enum_name),	\
		(DT_STRING_UPPER_TOKEN(id, enum_name)),	\
		(0xFF))

#define GEN_TYPE_ENTRY(id, compat, elem, enum)			\
	COND_CODE_1(DT_NODE_HAS_COMPAT(id, compat),		\
		(.step_size = DT_PROP_LEN(id, elem) + 1,	\
		 .driver = enum,),				\
		())

#define GEN_POLICY_ENTRY(id)					\
{								\
	GEN_TYPE_ENTRY(DT_PROP(id, led), COMPAT_GPIO,		\
		       gpios, LED_DRIVER_GPIO)			\
	GEN_TYPE_ENTRY(DT_PROP(id, led), COMPAT_PWM,		\
		       pwms, LED_DRIVER_PWM)			\
	.index = LED_TYPE_INDEX(DT_PHANDLE(id, led)),		\
	.id_enum = GEN_LED_ENUM(id),				\
	.count = ARRAY_SIZE(POLICY_CHECK_TAB(id)),		\
	.entries = POLICY_CHECK_TAB(id),			\
},

static const struct led_policy policy_table[] = {
DT_FOREACH_STATUS_OKAY(COMPAT_POLICY, GEN_POLICY_ENTRY)
};

/*
 * Policy state.
 * Stores the current state of the policy, such as the current
 * action and the timer for stepping through the color sets.
 */
struct led_state {
	struct k_timer timer;		/* Timer for cycles */
	const uint8_t *current_actions;	/* Ptr to current actions */
	uint8_t current_step;		/* Current step of actions */
	uint8_t actions_count;		/* Number of steps in actions */
};

static struct led_state led_state[ARRAY_SIZE(policy_table)];

/*
 * These functions use the driver type to call the
 * appropriate function for each type.
 * Poor man's polymorphism.
 */

/*
 * Set the LED to these colors.
 */
static void set_led_colors(const struct led_policy *lp, const uint8_t *colors)
{
	switch (lp->driver) {
	default:
		__ASSERT(false, "Unknown driver type %d", lp->driver);
		break;

#if DT_HAS_COMPAT_STATUS_OKAY(COMPAT_GPIO)
	case LED_DRIVER_GPIO:
		gpio_set_led_colors(lp->index, colors);
		break;
#endif
#if DT_HAS_COMPAT_STATUS_OKAY(COMPAT_PWM)
	case LED_DRIVER_PWM:
		pwm_set_led_colors(lp->index, colors);
		break;
#endif
	}
}

/*
 * Get the brightness max range. The brightness range is
 * an array of colors (enum ec_led_colors in ec_command.h),
 * and the values are returned depending on whether the
 * LED can display that color. The array is set to 1 for
 * GPIO LEDS, 255 for PWM LEDS, or 0 for unsupported LEDS.
 */
static void get_led_brightness_max(const struct led_policy *lp, uint8_t *br)
{
	switch (lp->driver) {
	default:
		__ASSERT(false, "Unknown driver type %d", lp->driver);
		break;

#if DT_HAS_COMPAT_STATUS_OKAY(COMPAT_GPIO)
	case LED_DRIVER_GPIO:
		gpio_get_led_brightness_max(lp->index, br);
		break;
#endif
#if DT_HAS_COMPAT_STATUS_OKAY(COMPAT_PWM)
	case LED_DRIVER_PWM:
		pwm_get_led_brightness_max(lp->index, br);
		break;
#endif
	}
}

/*
 * Set the brightness range. Turn on the selected color.
 */
static int set_led_brightness(const struct led_policy *lp, const uint8_t *br)
{
	switch (lp->driver) {
	default:
		__ASSERT(false, "Unknown driver type %d", lp->driver);
		return -1;

#if DT_HAS_COMPAT_STATUS_OKAY(COMPAT_GPIO)
	case LED_DRIVER_GPIO:
		gpio_set_led_brightness(lp->index, br);
		return 0;
#endif
#if DT_HAS_COMPAT_STATUS_OKAY(COMPAT_PWM)
	case LED_DRIVER_PWM:
		pwm_set_led_brightness(lp->index, br);
		return 0;
#endif
	}
}

/*
 * Colour cycle timer callback. Move to the next colour,
 * wrapping to the beginning if necessary, and restart the timer
 * with the new duration.
 */
static void led_timer(struct k_timer *timer)
{
	struct led_state *state = k_timer_user_data_get(timer);
	const struct led_policy *lp = &policy_table[state - led_state];
	const uint8_t *cp;

	/*
	 * Cycle to next colour in list. The number of colours matches
	 * the number of LEDs, and there is one more value for the
	 * duration (the last value in the step).
	 */
	state->current_step++;
	if (state->current_step >= state->actions_count) {
		state->current_step = 0;
	}
	cp = &state->current_actions[state->current_step * lp->step_size];
	k_timer_start(&state->timer, D_TICKS(cp[lp->step_size - 1]), K_FOREVER);
	set_led_colors(lp, cp);
}

/*
 * New action selected for this LED.
 */
static void new_action(const struct led_policy *lp,
		       struct led_state *state,
		       const struct led_policy_entry *entry)
{
	/*
	 * Stop the timer (no-op if not running).
	 */
	k_timer_stop(&state->timer);
	/*
	 * Set up the new action.
	 */
	state->current_actions = entry->actions;
	state->actions_count = entry->action_size / lp->step_size;
	state->current_step = 0;
	/*
	 * Set the LEDs to the new colors.
	 */
	set_led_colors(lp, entry->actions);
	/*
	 * start timer if multiple color sets are in the action.
	 */
	if (state->actions_count > 1) {
		k_timer_start(&state->timer,
			      D_TICKS(entry->actions[lp->step_size - 1]),
			      K_FOREVER);
	}
}

/*
 * Some event has occurred which may require updating the
 * LEDs. Iterate through the LED policies, using the first
 * matched policy to identify the action to be taken.
 */
static void update_leds(void)
{
	const struct led_policy *lp = policy_table;
	/*
	 * Go through all the policy tables.
	 * There may be multiple LEDs, each with a different policy.
	 */
	for (int i = 0; i < ARRAY_SIZE(policy_table); i++, lp++) {
		const struct led_policy_entry *e;
		struct led_state *state = &led_state[i];

		/*
		 * If the LED associated with this policy does
		 * not have auto control on, skip it.
		 */
		if (lp->id_enum != 0xFF &&
		    !led_auto_control_is_enabled(lp->id_enum)) {
			/*
			 * Shut down any auto control running.
			 */
			if (state->current_actions != NULL) {
				/*
				 * LED now under manual control,
				 * so stop timer and clear action.
				 */
				k_timer_stop(&state->timer);
				state->current_actions = NULL;
			}
			continue;
		}
		/*
		 * Iterate through the policy entries for this
		 * policy and see if any match.
		 */
		e = lp->entries;
		for (int j = 0; j < lp->count; j++, e++) {
			/* Check for AP state match */
			if (e->ap_state != LED_AP_ANY &&
			    e->ap_state != cpu_state) {
				continue;
			}
#if defined(CONFIG_CHARGER)
			/* Check for charger state match */
			if (e->charger_state != LED_CHARGER_ANY &&
			    e->charger_state != charger_state) {
				continue;
			}
#endif
#if defined(CONFIG_BATTERY)
			/* Check battery charge match */
			if (battery_state < e->battery[0] ||
			    battery_state > e->battery[1]) {
				continue;
			}
#endif
			/*
			 * Found a matching policy. If the
			 * attached action is not already running,
			 * apply the new action.
			 */
			if (state->current_actions != e->actions) {
				new_action(lp, state, e);
			}
			break;
		}
	}
}

#if defined(CONFIG_CHARGER) || defined(CONFIG_BATTERY)
/*
 * Poll the battery and charger every second and update
 * the LEDs.
 */
static void led_poll_inputs(void)
{
#if defined(CONFIG_CHARGER)
	switch (charge_get_state()) {
	default:
		break;
	case PWR_STATE_CHARGE:
		charger_state = LED_CHARGER_CHARGING;
		break;
	case PWR_STATE_DISCHARGE:
		charger_state = LED_CHARGER_DISCHARGING;
		break;
	case PWR_STATE_ERROR:
		charger_state = LED_CHARGER_ERROR;
		break;
	case PWR_STATE_IDLE:
		charger_state = LED_CHARGER_IDLE;
		break;
	case PWR_STATE_CHARGE_NEAR_FULL:
		charger_state = LED_CHARGER_FULL;
		break;
	}
#endif
#if defined(CONFIG_BATTERY)
	battery_state = charge_get_percent();
#endif
	update_leds();
}

DECLARE_HOOK(HOOK_SECOND, led_poll_inputs, HOOK_PRIO_DEFAULT);
#endif /* CONFIG_CHARGER || CONFIG_BATTERY */

/*
 * Callback for detecting changes to the AP state.
 * Update the cpu state and update the LEDs.
 */
static void cpu_update(struct ap_power_ev_callback *cb,
		       struct ap_power_ev_data data)
{
	switch (data.event) {
	default:
		break;

	case AP_POWER_RESUME:
		cpu_state = LED_AP_RUNNING;
		break;

	case AP_POWER_SUSPEND:
		cpu_state = LED_AP_SUSPENDED;
		break;

	case AP_POWER_SHUTDOWN:
		cpu_state = LED_AP_POWER_OFF;
		break;
	}
	/*
	 * Poll charger and battery so that they are
	 * up to date. led_poll_inputs() then calls
	 * update_leds().
	 */
	led_poll_inputs();
}

/*
 * Initialise the LED policy processing.
 */
static int init_led(const struct device *unused)
{
	static struct ap_power_ev_callback cb;

#if DT_HAS_COMPAT_STATUS_OKAY(COMPAT_GPIO)
	gpio_led_init();
#endif
	/*
	 * Initialise timers.
	 */
	for (int i = 0; i < ARRAY_SIZE(led_state); i++) {
		k_timer_init(&led_state[i].timer,
			     led_timer,
			     NULL);
		k_timer_user_data_set(&led_state[i].timer,
				      &led_state[i]);
	}
	ap_power_ev_init_callback(&cb, cpu_update,
				  AP_POWER_RESUME |
				  AP_POWER_SUSPEND |
				  AP_POWER_SHUTDOWN);
	ap_power_ev_add_callback(&cb);
	return 0;
}

SYS_INIT(init_led, APPLICATION, 1);

/*
 * API for EC host commands.
 */

/*
 * Generate array of supported LEDs.
 */
#define GEN_ID_ENUM(id)				\
	COND_CODE_1(DT_NODE_HAS_PROP(id, enum_name),	\
		(DT_STRING_UPPER_TOKEN(id, enum_name), ),	\
		())

const enum ec_led_id supported_led_ids[] = {
	DT_FOREACH_STATUS_OKAY(COMPAT_POLICY, GEN_ID_ENUM)
};

BUILD_ASSERT((sizeof(supported_led_ids) != 0),
	     "Must define at least one EC LED ID label");
const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

/*
 * Finds the LED policy associated with this LED enum ID.
 * Returns NULL if not found.
 */
static const struct led_policy *led_id_to_policy(enum ec_led_id led_id)
{
	for (int i = 0; i < ARRAY_SIZE(policy_table); i++) {
		if (policy_table[i].id_enum == led_id) {
			return &policy_table[i];
		}
	}
	return NULL;
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	const struct led_policy *lp = led_id_to_policy(led_id);

	if (lp != NULL) {
		get_led_brightness_max(lp, brightness_range);
	}
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	const struct led_policy *lp = led_id_to_policy(led_id);

	if (lp != NULL) {
		set_led_brightness(lp, brightness);
		return 0;
	}
	return -1;
}
