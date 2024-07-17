/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Button module for Chrome EC */

#include "atomic.h"
#include "button.h"
#include "chipset.h"
#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "keyboard_protocol.h"
#include "led_common.h"
#include "mkbp_input_devices.h"
#include "power_button.h"
#include "system.h"
#include "timer.h"
#include "util.h"
#include "watchdog.h"

/* Console output macro */
#define CPRINTS(format, args...) cprints(CC_SWITCH, format, ##args)

struct button_state_t {
	uint64_t debounce_time;
	int debounced_pressed;
};

static struct button_state_t state[BUTTON_COUNT];

static uint64_t next_deferred_time;

#if defined(CONFIG_CMD_BUTTON) || defined(CONFIG_HOSTCMD_BUTTON)
#define CONFIG_SIMULATED_BUTTON
#endif

#ifdef CONFIG_SIMULATED_BUTTON
/* Bitmask to keep track of simulated state of each button.
 * Bit numbers are aligned to enum button.
 */
static atomic_t sim_button_state;

/*
 * Flip state of associated button type in sim_button_state bitmask.
 * In bitmask, if bit is 1, button is pressed.  If bit is 0, button is
 * released.
 *
 * Returns the appropriate GPIO value based on table below:
 * +----------+--------+--------+
 * |  state   | active | return |
 * +----------+--------+--------+
 * | pressed  |  high  |   1    |
 * | pressed  |  low   |   0    |
 * | released |  high  |   0    |
 * | released |  low   |   1    |
 * +----------+--------+--------+
 */
static int simulated_button_pressed(const struct button_config *button)
{
	return !!((uint32_t)sim_button_state & BIT(button->type));
}
#endif

/*
 * Whether a button is currently pressed.
 */
static int raw_button_pressed(const struct button_config *button)
{
	int physical_value = 0;
	int simulated_value = 0;
	if (!(button->flags & BUTTON_FLAG_DISABLED)) {
		if (IS_ENABLED(CONFIG_ADC_BUTTONS) &&
		    button_is_adc_detected(button->gpio)) {
			physical_value = adc_to_physical_value(button->gpio);
		} else {
			physical_value =
				(!!gpio_get_level(button->gpio) ==
				 !!(button->flags & BUTTON_FLAG_ACTIVE_HIGH));
		}
#ifdef CONFIG_SIMULATED_BUTTON
		simulated_value = simulated_button_pressed(button);
#endif
	}

	return (simulated_value || physical_value);
}

#ifdef CONFIG_BUTTON_TRIGGERED_RECOVERY

#ifdef CONFIG_DETACHABLE_BASE
static void button_blink_hw_reinit_led(void)
{
	int led_state = LED_STATE_ON;
	timestamp_t deadline;
	timestamp_t now = get_time();

	/* Blink LED for 3 seconds. */
	deadline.val = now.val + (3 * SECOND);

	while (!timestamp_expired(deadline, &now)) {
		led_control(EC_LED_ID_RECOVERY_HW_REINIT_LED, led_state);
		led_state = !led_state;
		watchdog_reload();
		crec_msleep(100);
		now = get_time();
	}

	/* Reset LED to default state. */
	led_control(EC_LED_ID_RECOVERY_HW_REINIT_LED, LED_STATE_RESET);
}
#endif

/*
 * Whether recovery button (or combination of equivalent buttons) is pressed
 * If a dedicated recovery button is used, any of the buttons can be pressed,
 * otherwise, all the buttons must be pressed.
 */
static int is_recovery_button_pressed(void)
{
	int i, pressed;
	for (i = 0; i < recovery_buttons_count; i++) {
		pressed = raw_button_pressed(recovery_buttons[i]);
		if (IS_ENABLED(CONFIG_DEDICATED_RECOVERY_BUTTON)) {
			if (pressed)
				return 1;
		} else {
			if (!pressed)
				return 0;
		}
	}
	return IS_ENABLED(CONFIG_DEDICATED_RECOVERY_BUTTON) ? 0 : 1;
}

/*
 * If the EC is reset and recovery is requested, then check if HW_REINIT is
 * requested as well. Since the EC reset occurs after volup+voldn+power buttons
 * are held down for 10 seconds, check the state of these buttons for 20 more
 * seconds. If they are still held down all this time, then set host event to
 * indicate HW_REINIT is requested. Also, make sure watchdog is reloaded in
 * order to prevent watchdog from resetting the EC.
 */
static void button_check_hw_reinit_required(void)
{
	timestamp_t deadline;
	timestamp_t now = get_time();
#ifdef CONFIG_DETACHABLE_BASE
	uint8_t led_on = 0;
#endif

	deadline.val = now.val + (20 * SECOND);

	CPRINTS("Checking for HW_REINIT request");

	while (!timestamp_expired(deadline, &now)) {
		if (!is_recovery_button_pressed() ||
		    !power_button_signal_asserted()) {
			CPRINTS("No HW_REINIT request");
#ifdef CONFIG_DETACHABLE_BASE
			if (led_on)
				led_control(EC_LED_ID_RECOVERY_HW_REINIT_LED,
					    LED_STATE_RESET);
#endif
			return;
		}

#ifdef CONFIG_DETACHABLE_BASE
		if (!led_on) {
			led_control(EC_LED_ID_RECOVERY_HW_REINIT_LED,
				    LED_STATE_ON);
			led_on = 1;
		}
#endif

		now = get_time();
		watchdog_reload();
	}

	CPRINTS("HW_REINIT requested");
	host_set_single_event(EC_HOST_EVENT_KEYBOARD_RECOVERY_HW_REINIT);

#ifdef CONFIG_DETACHABLE_BASE
	button_blink_hw_reinit_led();
#endif
}

static int is_recovery_boot(void)
{
	if (system_jumped_to_this_image())
		return 0;
	if (!(system_get_reset_flags() &
	      (EC_RESET_FLAG_RESET_PIN | EC_RESET_FLAG_POWER_ON)))
		return 0;
	if (!is_recovery_button_pressed())
		return 0;
	return 1;
}
#endif /* CONFIG_BUTTON_TRIGGERED_RECOVERY */

static void button_reset(enum button button_type,
			 const struct button_config *button)
{
	state[button_type].debounced_pressed = raw_button_pressed(button);
	state[button_type].debounce_time = 0;
	gpio_enable_interrupt(button->gpio);
}

static uint32_t boot_button;

uint32_t button_get_boot_button(void)
{
	return boot_button;
}

test_export_static void boot_button_set(enum button button)
{
	boot_button |= BIT(button);
}

test_export_static void boot_button_clear(enum button button)
{
	boot_button &= ~BIT(button);
}

/*
 * Button initialization.
 */
void button_init(void)
{
	int i;

	CPRINTS("init buttons");
	next_deferred_time = 0;
	for (i = 0; i < BUTTON_COUNT; i++)
		button_reset(i, &buttons[i]);

#ifdef CONFIG_BUTTON_TRIGGERED_RECOVERY
	if (is_recovery_boot()) {
		system_clear_reset_flags(EC_RESET_FLAG_AP_OFF);
		host_set_single_event(EC_HOST_EVENT_KEYBOARD_RECOVERY);
		button_check_hw_reinit_required();
	}
#endif /* defined(CONFIG_BUTTON_TRIGGERED_RECOVERY) */

	/* Detect boot buttons. */
	for (i = 0; i < BUTTON_COUNT; i++) {
		if (raw_button_pressed(&buttons[i]))
			boot_button_set(i);
	}
	CPRINTS("boot buttons: 0x%x", boot_button);
}

#ifdef CONFIG_BUTTONS_RUNTIME_CONFIG
int button_reassign_gpio(enum button button_type, enum gpio_signal gpio)
{
	if (button_type >= BUTTON_COUNT)
		return EC_ERROR_INVAL;

	/* Disable currently assigned interrupt */
	gpio_disable_interrupt(buttons[button_type].gpio);

	/* Reconfigure GPIO and enable the new interrupt */
	buttons[button_type].gpio = gpio;
	button_reset(button_type, &buttons[button_type]);

	return EC_SUCCESS;
}

int button_disable_gpio(enum button button_type)
{
	if (button_type >= BUTTON_COUNT)
		return EC_ERROR_INVAL;

	/* Disable GPIO interrupt */
	gpio_disable_interrupt(buttons[button_type].gpio);
	/* Mark button as disabled */
	buttons[button_type].flags |= BUTTON_FLAG_DISABLED;

	return EC_SUCCESS;
}
#endif

/*
 * Handle debounced button changing state.
 */

static void button_change_deferred(void);
DECLARE_DEFERRED(button_change_deferred);

#ifdef CONFIG_EMULATED_SYSRQ
static void debug_mode_handle(void);
DECLARE_DEFERRED(debug_mode_handle);
DECLARE_HOOK(HOOK_POWER_BUTTON_CHANGE, debug_mode_handle, HOOK_PRIO_LAST);
#endif

static void button_change_deferred(void)
{
	int i;
	int new_pressed;
	uint64_t soonest_debounce_time = 0;
	uint64_t time_now = get_time().val;

	for (i = 0; i < BUTTON_COUNT; i++) {
		/* Skip this button if we are not waiting to debounce */
		if (state[i].debounce_time == 0)
			continue;

		if (state[i].debounce_time <= time_now) {
			/* Check if the state has changed */
			new_pressed = raw_button_pressed(&buttons[i]);
			if (state[i].debounced_pressed != new_pressed) {
				state[i].debounced_pressed = new_pressed;
#ifdef CONFIG_EMULATED_SYSRQ
				/*
				 * Calling deferred function for handling debug
				 * mode so that button change processing is not
				 * delayed.
				 */
#ifdef CONFIG_DEDICATED_RECOVERY_BUTTON
				/*
				 * Only the direct signal is used for sysrq.
				 * H1_EC_RECOVERY_BTN_ODL doesn't reflect the
				 * true state of the recovery button.
				 */
				if (i == BUTTON_RECOVERY)
#endif
					hook_call_deferred(
						&debug_mode_handle_data, 0);
#endif
				CPRINTS("Button '%s' was %s", buttons[i].name,
					new_pressed ? "pressed" : "released");
				if (!new_pressed)
					boot_button_clear(i);
				if (IS_ENABLED(CONFIG_MKBP_INPUT_DEVICES)) {
					mkbp_button_update(buttons[i].type,
							   new_pressed);
				} else if (IS_ENABLED(HAS_TASK_KEYPROTO)) {
					keyboard_update_button(buttons[i].type,
							       new_pressed);
				}
			}

			/* Clear the debounce time to stop checking it */
			state[i].debounce_time = 0;
		} else {
			/*
			 * Make sure the next deferred call happens on or before
			 * each button needs it.
			 */
			soonest_debounce_time =
				(soonest_debounce_time == 0) ?
					state[i].debounce_time :
					MIN(soonest_debounce_time,
					    state[i].debounce_time);
		}
	}

	if (soonest_debounce_time != 0) {
		next_deferred_time = soonest_debounce_time;
		hook_call_deferred(&button_change_deferred_data,
				   next_deferred_time - time_now);
	}
}

static atomic_val_t pending_irqs;

/* bottom half of irq handler */
void button_irq_handler(void)
{
	uint64_t time_now = get_time().val;
	int irqs = atomic_clear(&pending_irqs);

	for (int i = 0; i < BUTTON_COUNT; i++) {
		if ((irqs & BIT(i)) == 0 ||
		    (buttons[i].flags & BUTTON_FLAG_DISABLED))
			continue;

		state[i].debounce_time = time_now + buttons[i].debounce_us;
		if (next_deferred_time <= time_now ||
		    next_deferred_time > state[i].debounce_time) {
			next_deferred_time = state[i].debounce_time;
			hook_call_deferred(&button_change_deferred_data,
					   next_deferred_time - time_now);
		}
	}
}
DECLARE_DEFERRED(button_irq_handler);

/*
 * Handle a button interrupt.
 */
void button_interrupt(enum gpio_signal signal)
{
	for (int i = 0; i < BUTTON_COUNT; i++) {
		if (buttons[i].gpio != signal ||
		    (buttons[i].flags & BUTTON_FLAG_DISABLED))
			continue;

		atomic_or(&pending_irqs, BIT(i));
		hook_call_deferred(&button_irq_handler_data, 0);
		break;
	}
}

#ifdef CONFIG_SIMULATED_BUTTON
static int button_present(enum keyboard_button_type type)
{
	int i;

	for (i = 0; i < BUTTON_COUNT; i++)
		if (buttons[i].type == type)
			break;

	return i;
}

static void button_interrupt_simulate(int button)
{
	button_interrupt(buttons[button].gpio);
}

static void simulate_button_release_deferred(void)
{
	int button_idx;

	/* Release the button */
	for (button_idx = 0; button_idx < BUTTON_COUNT; button_idx++) {
		/* Check state for button pressed */
		if ((uint32_t)sim_button_state &
		    BIT(buttons[button_idx].type)) {
			/* Set state of the button as released */
			atomic_clear_bits(&sim_button_state,
					  BIT(buttons[button_idx].type));

			button_interrupt_simulate(button_idx);
		}
	}
}
DECLARE_DEFERRED(simulate_button_release_deferred);

static void simulate_button(uint32_t button_mask, int press_ms)
{
	int button_idx;

	/* Press the button */
	for (button_idx = 0; button_idx < BUTTON_COUNT; button_idx++) {
		if (button_mask & BIT(button_idx)) {
			/* Set state of the button as pressed */
			atomic_or(&sim_button_state,
				  BIT(buttons[button_idx].type));

			button_interrupt_simulate(button_idx);
		}
	}

	/* Defer the button release for specified duration */
	hook_call_deferred(&simulate_button_release_deferred_data,
			   press_ms * MSEC);
}
#endif /* #ifdef CONFIG_SIMULATED_BUTTON */

#ifdef CONFIG_CMD_BUTTON
static int console_command_button(int argc, const char **argv)
{
	int press_ms = 50;
	char *e;
	int argv_idx;
	int button = BUTTON_COUNT;
	uint32_t button_mask = 0;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	for (argv_idx = 1; argv_idx < argc; argv_idx++) {
		if (!strcasecmp(argv[argv_idx], "vup"))
			button = button_present(KEYBOARD_BUTTON_VOLUME_UP);
		else if (!strcasecmp(argv[argv_idx], "vdown"))
			button = button_present(KEYBOARD_BUTTON_VOLUME_DOWN);
		else if (!strcasecmp(argv[argv_idx], "rec"))
			button = button_present(KEYBOARD_BUTTON_RECOVERY);
		else {
			/* If last parameter check if it is an integer. */
			if (argv_idx == argc - 1) {
				press_ms = strtoi(argv[argv_idx], &e, 0);
				/* If integer, break out of the loop. */
				if (!*e)
					break;
			}
			button = BUTTON_COUNT;
		}

		if (button == BUTTON_COUNT)
			return EC_ERROR_PARAM1 + argv_idx - 1;

		button_mask |= BIT(button);
	}

	if (!button_mask)
		return EC_SUCCESS;

	simulate_button(button_mask, press_ms);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(button, console_command_button, "vup|vdown|rec msec",
			"Simulate button press");
#endif /* CONFIG_CMD_BUTTON */

#ifdef CONFIG_HOSTCMD_BUTTON
static enum ec_status host_command_button(struct host_cmd_handler_args *args)
{
	const struct ec_params_button *p = args->params;
	int idx;
	uint32_t button_mask = 0;

	/* Only available on unlocked systems */
	if (system_is_locked())
		return EC_RES_ACCESS_DENIED;

	for (idx = 0; idx < KEYBOARD_BUTTON_COUNT; idx++) {
		if (p->btn_mask & BIT(idx))
			button_mask |= BIT(button_present(idx));
	}

	simulate_button(button_mask, p->press_ms);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_BUTTON, host_command_button, EC_VER_MASK(0));

#endif /* CONFIG_HOSTCMD_BUTTON */

#ifdef CONFIG_EMULATED_SYSRQ

#ifdef CONFIG_DEDICATED_RECOVERY_BUTTON

/*
 * Simplified sysrq handler
 *
 * In simplified sysrq, user can
 * - press and release recovery button to send one sysrq event to the host
 * - press and hold recovery button for 4 seconds to reset the AP (warm reset)
 */
static void debug_mode_handle(void)
{
	static int recovery_button_pressed = 0;

	if (!recovery_button_pressed) {
		if (is_recovery_button_pressed()) {
			/* User pressed recovery button. Wait for 4 seconds
			 * to see if warm reset is requested. */
			recovery_button_pressed = 1;
			hook_call_deferred(&debug_mode_handle_data, 4 * SECOND);
		}
	} else {
		/* We come here when recovery button is released or when
		 * 4 sec elapsed with recovery button still pressed. */
		if (!is_recovery_button_pressed()) {
			/* Cancel pending timer */
			hook_call_deferred(&debug_mode_handle_data, -1);
			host_send_sysrq('x');
			CPRINTS("DEBUG MODE: sysrq-x sent");
		} else {
			chipset_reset(CHIPSET_RESET_DBG_WARM_REBOOT);
			CPRINTS("DEBUG MODE: Warm reset triggered");
		}
		recovery_button_pressed = 0;
	}
}

#else /* CONFIG_DEDICATED_RECOVERY_BUTTON */

#define DEBUG_BTN_POWER BIT(0)
#define DEBUG_BTN_VOL_UP BIT(1)
#define DEBUG_BTN_VOL_DN BIT(2)
#define DEBUG_TIMEOUT (10 * SECOND)

static enum debug_state curr_debug_state = STATE_DEBUG_NONE;
static enum debug_state next_debug_state = STATE_DEBUG_NONE;
static timestamp_t debug_state_deadline;
static int debug_button_hit_count;

static int debug_button_mask(void)
{
	int mask = 0;

	/* Get power button state */
	if (power_button_is_pressed())
		mask |= DEBUG_BTN_POWER;
#ifdef CONFIG_VOLUME_BUTTONS
	/* Get volume up state */
	if (state[BUTTON_VOLUME_UP].debounced_pressed)
		mask |= DEBUG_BTN_VOL_UP;

	/* Get volume down state */
	if (state[BUTTON_VOLUME_DOWN].debounced_pressed)
		mask |= DEBUG_BTN_VOL_DN;
#endif /* CONFIG_VOLUME_BUTTONS */
	return mask;
}

static int debug_button_pressed(int mask)
{
	return debug_button_mask() == mask;
}

#ifdef CONFIG_DETACHABLE_BASE
static int debug_mode_blink_led(void)
{
	return ((curr_debug_state != STATE_DEBUG_NONE) &&
		(curr_debug_state != STATE_DEBUG_CHECK));
}
#endif /* CONFIG_DETACHABLE_BASE */

static void debug_mode_transition(enum debug_state next_state)
{
	timestamp_t now = get_time();
#ifdef CONFIG_DETACHABLE_BASE
	int curr_blink_state = debug_mode_blink_led();
#endif /* CONFIG_DETACHABLE_BASE */

	/* Cancel any deferred calls. */
	hook_call_deferred(&debug_mode_handle_data, -1);

	/* Update current debug mode state. */
	curr_debug_state = next_state;

	/* Set deadline to 10seconds from current time. */
	debug_state_deadline.val = now.val + DEBUG_TIMEOUT;

	switch (curr_debug_state) {
	case STATE_DEBUG_NONE:
		/*
		 * Nothing is done here since some states can transition to
		 * STATE_DEBUG_NONE in this function. Wait until all other
		 * states are evaluated to take the action for STATE_NONE.
		 */
		break;
	case STATE_DEBUG_CHECK:
	case STATE_STAGING:
		break;
	case STATE_DEBUG_MODE_ACTIVE:
		debug_button_hit_count = 0;
		break;
	case STATE_SYSRQ_PATH:
		/*
		 * Increment debug_button_hit_count and ensure it does not go
		 * past 3. If it exceeds the limit transition to STATE_NONE.
		 */
		debug_button_hit_count++;
		if (debug_button_hit_count == 4)
			curr_debug_state = STATE_DEBUG_NONE;
		break;
	case STATE_WARM_RESET_PATH:
		break;
	case STATE_SYSRQ_EXEC:
		/*
		 * Depending upon debug_button_hit_count, send appropriate
		 * number of sysrq events to host and transition to STATE_NONE.
		 */
		while (debug_button_hit_count) {
			host_send_sysrq('x');
			CPRINTS("DEBUG MODE: sysrq-x sent");
			debug_button_hit_count--;
		}
		curr_debug_state = STATE_DEBUG_NONE;
		break;
	case STATE_WARM_RESET_EXEC:
		/* Warm reset the host and transition to STATE_NONE. */
		chipset_reset(CHIPSET_RESET_DBG_WARM_REBOOT);
		CPRINTS("DEBUG MODE: Warm reset triggered");
		curr_debug_state = STATE_DEBUG_NONE;
		break;
	default:
		curr_debug_state = STATE_DEBUG_NONE;
	}

	if (curr_debug_state != STATE_DEBUG_NONE) {
		/*
		 * Schedule a deferred call after DEBUG_TIMEOUT to check for
		 * button state if it does not change during the timeout
		 * duration.
		 */
		hook_call_deferred(&debug_mode_handle_data, DEBUG_TIMEOUT);
		return;
	}

	/* If state machine reached initial state, reset all variables. */
	CPRINTS("DEBUG MODE: Exit!");
	next_debug_state = STATE_DEBUG_NONE;
	debug_state_deadline.val = 0;
	debug_button_hit_count = 0;
#ifdef CONFIG_DETACHABLE_BASE
	if (curr_blink_state)
		led_control(EC_LED_ID_SYSRQ_DEBUG_LED, LED_STATE_RESET);
#endif /* CONFIG_DETACHABLE_BASE */
}

__test_only void reset_button_debug_state(void)
{
	debug_mode_transition(STATE_DEBUG_NONE);
}

__test_only enum debug_state get_button_debug_state(void)
{
	return curr_debug_state;
}

static void debug_mode_handle(void)
{
	int mask;

	switch (curr_debug_state) {
	case STATE_DEBUG_NONE:
		/*
		 * If user pressed Vup+Vdn, check for next 10 seconds to see if
		 * user keeps holding the keys.
		 */
		if (debug_button_pressed(DEBUG_BTN_VOL_UP | DEBUG_BTN_VOL_DN))
			debug_mode_transition(STATE_DEBUG_CHECK);
		break;
	case STATE_DEBUG_CHECK:
		/*
		 * If no key is pressed or any key combo other than Vup+Vdn is
		 * held, then quit debug check mode.
		 */
		if (!debug_button_pressed(DEBUG_BTN_VOL_UP | DEBUG_BTN_VOL_DN))
			debug_mode_transition(STATE_DEBUG_NONE);
		else if (timestamp_expired(debug_state_deadline, NULL)) {
			/*
			 * If Vup+Vdn are held down for 10 seconds, then its
			 * time to enter debug mode.
			 */
			CPRINTS("DEBUG MODE: Active!");
			next_debug_state = STATE_DEBUG_MODE_ACTIVE;
			debug_mode_transition(STATE_STAGING);
		}
		break;
	case STATE_STAGING:
		mask = debug_button_mask();

		/* If no button is pressed, transition to next state. */
		if (!mask) {
			debug_mode_transition(next_debug_state);
			return;
		}

		/* Exit debug mode if keys are stuck for > 10 seconds. */
		if (timestamp_expired(debug_state_deadline, NULL))
			debug_mode_transition(STATE_DEBUG_NONE);
		else {
			timestamp_t now = get_time();

			/*
			 * Schedule a deferred call in case timeout hasn't
			 * occurred yet.
			 */
			hook_call_deferred(
				&debug_mode_handle_data,
				(debug_state_deadline.val - now.val));
		}

		break;
	case STATE_DEBUG_MODE_ACTIVE:
		mask = debug_button_mask();

		/*
		 * Continue in this state if button is not pressed and timeout
		 * has not occurred.
		 */
		if (!mask && !timestamp_expired(debug_state_deadline, NULL))
			return;

		/* Exit debug mode if valid buttons are not pressed. */
		if ((mask != DEBUG_BTN_VOL_UP) && (mask != DEBUG_BTN_VOL_DN)) {
			debug_mode_transition(STATE_DEBUG_NONE);
			return;
		}

		/*
		 * Transition to STAGING state with next state set to:
		 * 1. SYSRQ_PATH     : If Vup was pressed.
		 * 2. WARM_RESET_PATH: If Vdn was pressed.
		 */
		if (mask == DEBUG_BTN_VOL_UP)
			next_debug_state = STATE_SYSRQ_PATH;
		else
			next_debug_state = STATE_WARM_RESET_PATH;

		debug_mode_transition(STATE_STAGING);
		break;
	case STATE_SYSRQ_PATH:
		mask = debug_button_mask();

		/*
		 * Continue in this state if button is not pressed and timeout
		 * has not occurred.
		 */
		if (!mask && !timestamp_expired(debug_state_deadline, NULL))
			return;

		/* Exit debug mode if valid buttons are not pressed. */
		if ((mask != DEBUG_BTN_VOL_UP) && (mask != DEBUG_BTN_VOL_DN)) {
			debug_mode_transition(STATE_DEBUG_NONE);
			return;
		}

		if (mask == DEBUG_BTN_VOL_UP) {
			/*
			 * Else transition to STAGING state with next state set
			 * to SYSRQ_PATH.
			 */
			next_debug_state = STATE_SYSRQ_PATH;
		} else {
			/*
			 * Else if Vdn is pressed, transition to STAGING with
			 * next state set to SYSRQ_EXEC.
			 */
			next_debug_state = STATE_SYSRQ_EXEC;
		}
		debug_mode_transition(STATE_STAGING);
		break;
	case STATE_WARM_RESET_PATH:
		mask = debug_button_mask();

		/*
		 * Continue in this state if button is not pressed and timeout
		 * has not occurred.
		 */
		if (!mask && !timestamp_expired(debug_state_deadline, NULL))
			return;

		/* Exit debug mode if valid buttons are not pressed. */
		if (mask != DEBUG_BTN_VOL_UP) {
			debug_mode_transition(STATE_DEBUG_NONE);
			return;
		}

		next_debug_state = STATE_WARM_RESET_EXEC;
		debug_mode_transition(STATE_STAGING);
		break;
	case STATE_SYSRQ_EXEC:
	case STATE_WARM_RESET_EXEC:
	default:
		debug_mode_transition(STATE_DEBUG_NONE);
		break;
	}
}

#ifdef CONFIG_DETACHABLE_BASE
static void debug_led_tick(void)
{
	static int led_state = LED_STATE_OFF;

	if (debug_mode_blink_led()) {
		led_state = !led_state;
		led_control(EC_LED_ID_SYSRQ_DEBUG_LED, led_state);
	}
}
DECLARE_HOOK(HOOK_TICK, debug_led_tick, HOOK_PRIO_DEFAULT);
#endif /* CONFIG_DETACHABLE_BASE */

#endif /* !CONFIG_DEDICATED_RECOVERY_BUTTON */
#endif /* CONFIG_EMULATED_SYSRQ */

#ifndef CONFIG_BUTTONS_RUNTIME_CONFIG
const struct button_config buttons[BUTTON_COUNT] = {
#else
struct button_config buttons[BUTTON_COUNT] = {
#endif
#ifdef CONFIG_VOLUME_BUTTONS
	[BUTTON_VOLUME_UP] = {
		.name = "Volume Up",
		.type = KEYBOARD_BUTTON_VOLUME_UP,
		.gpio = GPIO_VOLUME_UP_L,
		.debounce_us = BUTTON_DEBOUNCE_US,
		.flags = 0,
	},

	[BUTTON_VOLUME_DOWN] = {
		.name = "Volume Down",
		.type = KEYBOARD_BUTTON_VOLUME_DOWN,
		.gpio = GPIO_VOLUME_DOWN_L,
		.debounce_us = BUTTON_DEBOUNCE_US,
		.flags = 0,
	},

#endif
#if defined(CONFIG_DEDICATED_RECOVERY_BUTTON)
#ifndef CONFIG_DEDICATED_RECOVERY_BUTTON_FLAGS
#define CONFIG_DEDICATED_RECOVERY_BUTTON_FLAGS 0
#endif
	[BUTTON_RECOVERY] = {
		.name = "Recovery",
		.type = KEYBOARD_BUTTON_RECOVERY,
		.gpio = GPIO_RECOVERY_L,
		.debounce_us = BUTTON_DEBOUNCE_US,
		.flags = CONFIG_DEDICATED_RECOVERY_BUTTON_FLAGS,
	},
#ifdef CONFIG_DEDICATED_RECOVERY_BUTTON_2
#ifndef CONFIG_DEDICATED_RECOVERY_BUTTON_2_FLAGS
#define CONFIG_DEDICATED_RECOVERY_BUTTON_2_FLAGS 0
#endif
	[BUTTON_RECOVERY_2] = {
		.name = "Recovery2",
		.type = KEYBOARD_BUTTON_RECOVERY,
		.gpio = GPIO_RECOVERY_L_2,
		.debounce_us = BUTTON_DEBOUNCE_US,
		.flags = CONFIG_DEDICATED_RECOVERY_BUTTON_2_FLAGS,
	}
#endif /* defined(CONFIG_DEDICATED_RECOVERY_BUTTON_2) */
#endif /* defined(CONFIG_DEDICATED_RECOVERY_BUTTON) */
};

#ifdef CONFIG_BUTTON_TRIGGERED_RECOVERY
/*
 * Prefer the dedicated recovery button over the volume buttons if
 * both are present.
 */
const struct button_config *recovery_buttons[] = {
#ifdef CONFIG_DEDICATED_RECOVERY_BUTTON
	&buttons[BUTTON_RECOVERY],

#ifdef CONFIG_DEDICATED_RECOVERY_BUTTON_2
	&buttons[BUTTON_RECOVERY_2],
#endif /* defined(CONFIG_BUTTON_TRIGGERED_RECOVERY_2) */

#elif defined(CONFIG_VOLUME_BUTTONS)
	&buttons[BUTTON_VOLUME_DOWN],
	&buttons[BUTTON_VOLUME_UP],
#endif /* defined(CONFIG_VOLUME_BUTTONS) */
};
const int recovery_buttons_count = ARRAY_SIZE(recovery_buttons);
#endif /* defined(CONFIG_BUTTON_TRIGGERED_RECOVERY) */
