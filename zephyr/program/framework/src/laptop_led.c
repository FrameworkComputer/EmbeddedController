#include "battery.h"
#include "board_led.h"
#include "board_function.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "chipset.h"
#include "cypress_pd_common.h"
#include "diagnostics.h"
#include "ec_commands.h"
#include "extpower.h"
#include "hooks.h"
#include "host_command.h"
#include "led.h"
#include "led_common.h"
#include "lid_switch.h"
#include "power.h"
#include "power_sequence.h"
#include "system.h"
#include "util.h"

#ifdef CONFIG_PLATFORM_EC_FRAMEWORK_LAPTOP_16
#include "gpu.h"
#include "input_module.h"
#endif

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

const enum ec_led_id supported_led_ids[] = { EC_LED_ID_BATTERY_LED,
					     EC_LED_ID_POWER_LED };

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

bool multifunction_leds_control(void)
{
	int colors[3] = {LED_OFF, LED_OFF, LED_OFF};

	/* In facotry mode, don't control led */
	if (!led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED))
		return false;

	/* Debug Active */
	if (diagnostics_tick())
		return true;

	/* Battery disconnect active signal */
	if (battery_is_cut_off()) {
		colors[0] = LED_RED;
		colors[1] = LED_BLUE;
		colors[2] = LED_OFF;
		customized_leds_set_color(colors, 2, CONFIG_PLATFORM_MULTI_LED_FREQ,
			EC_LED_ID_BATTERY_LED);
		return true;
	}

	/* Battery is not present, ignored if in standalone mode */
	if ((battery_is_present() != BP_YES) && !get_standalone_mode()) {
		colors[0] = LED_RED;
		colors[1] = LED_BLUE;
		colors[2] = LED_OFF;
		customized_leds_set_color(colors, 2, CONFIG_PLATFORM_MULTI_LED_FREQ,
			EC_LED_ID_BATTERY_LED);
		return true;
	}

	/* C cover detect switch open */
	if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_chassis_open_l)) == 0 &&
		!get_standalone_mode()) {

		colors[0] = LED_RED;
		colors[1] = LED_OFF;
		colors[2] = LED_OFF;
		customized_leds_set_color(colors, 2, 1000, EC_LED_ID_BATTERY_LED);
		return true;
	}

#ifdef CONFIG_PLATFORM_EC_FRAMEWORK_LAPTOP_16
	/* GPU bay cover detect switch open */
	if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_f_beam_open_l)) == 0 &&
		!get_standalone_mode()) {
		colors[0] = LED_RED;
		colors[1] = LED_AMBER;
		colors[2] = LED_OFF;
		customized_leds_set_color(colors, 3, 1000, EC_LED_ID_BATTERY_LED);
		return true;
	}

	/* GPU Bay Module Fault */
	if (gpu_module_fault() && extpower_is_present()) {
		colors[0] = LED_RED;
		colors[1] = LED_AMBER;
		colors[2] = LED_OFF;
		customized_leds_set_color(colors, 3, 1000, EC_LED_ID_BATTERY_LED);
		return true;
	}

	/* Input Deck not fully populated */
	if (!input_deck_is_fully_populated() && !get_standalone_mode() &&
		!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		colors[0] = LED_RED;
		colors[1] = LED_BLUE;
		colors[2] = LED_OFF;
		customized_leds_set_color(colors, 3, 500, EC_LED_ID_BATTERY_LED);
		return true;
	}
#endif

	return false;
}

bool power_button_led_control(void)
{
	int colors[3] = {LED_OFF, LED_OFF, LED_OFF};

	/* In facotry mode, don't control led */
	if (!led_auto_control_is_enabled(EC_LED_ID_POWER_LED))
		return false;

	/* Turn off fingerprint LED when lid is closed */
	if (!IS_ENABLED(CONFIG_PLATFORM_EC_TABLET_MODE) && !lid_is_open()) {
		led_set_color(LED_OFF, EC_LED_ID_POWER_LED);
		return true;
	}

	if (chipset_in_state(CHIPSET_STATE_ON) && (charge_get_percent() < 3) &&
		!extpower_is_present()) {
		colors[0] = LED_WHITE;
		colors[1] = LED_OFF;
		colors[2] = LED_OFF;
		customized_leds_set_color(colors, 2, 500, EC_LED_ID_POWER_LED);
		return true;
	}

	return false;
}
