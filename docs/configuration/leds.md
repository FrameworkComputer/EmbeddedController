# Configure LEDs

LEDs provide status about the following:

- Dedicated battery state/charging state
- Chromebook power
- Adapter power
- Left side USB-C port (battery state/charging state)
- Right side USB-C port (battery state/charging state)
- Recovery mode
- Debug mode

LEDs can be configured as simple GPIOs, with on/off control only, or as PWM with
adjustment brightness and color.

## Config options

In [config.h], search for options that start with `CONFIG_LED*` and evaluate
whether each option is appropriate to add to `baseboard.h` or `board.h`.

- `CONFIG_LED_COMMON` - Should be defined for both GPIO and PWM style LEDs.
- `CONFIG_LED_ONOFF_STATES` - used for GPIO controlled LEDs
- `CONFIG_LED_PWM` - used for PWM controlled LEDs.  You must also define
  `CONFIG_PWM` when using PWM controlled LEDs.

## Feature Parameters

- `CONFIG_LED_PWM_COUNT <count>` - Must be defined when using PWM LEDs

Override the following parameters when using PWM LEDs if you don't want to use
the recommended LED color settings.
- `CONFIG_LED_PWM_CHARGE_COLOR <ec_led_color>`
- `CONFIG_LED_PWM_NEAR_FULL_COLOR <ec_led_color>`
- `CONFIG_LED_PWM_CHARGE_ERROR_COLOR <ec_led_color>`
- `CONFIG_LED_PWM_SOC_ON_COLOR <ec_led_color>`
- `CONFIG_LED_PWM_SOC_SUSPEND_COLOR <ec_led_color>`
- `CONFIG_LED_PWM_LOW_BATT_COLOR <ec_led_color>`

## GPIOs and Alternate Pins

For GPIO based LEDs, create `GPIO()` entries for all signals that connect to
platform LEDs. The default state of the pins should be set so that the LED is
off (typically high output).

For PWM LEDs, configure the `ALTERNATE()` macro, setting the module type to
`MODULE_PWM`.

## Data structures

For GPIO based LEDs:
- `struct led_descriptor led_bat_state_table[LED_NUM_STATES][LED_NUM_PHASES]` -
  Must be defined when `CONFIG_LED_ONOFF_STATES` is used. Defines the LED states
  for the platform for various charging states.

For PWM based LEDs:
- `const enum ec_led_id supported_led_ids[]` - Defines the LED type for all PWM
  LEDs in the system.  See [./include/ec_commands.h] for a description of the
  supported LED types.
- `struct pwm_led led_color_map[]` - Defines the PWM intensity of the individual
  LEDs to generate the corresponding color. This table allows for custom tuning
  of the LED brightness and color.
- `const struct pwm_channels[]` - Configures the PWM module, refer to the
  [Configuring PWM](./pwm.md) section for details.

See the [GPIO](./gpio.md) documentation for additional details on the GPIO
macros.

## Tasks

None required by this feature.

## Testing and Debugging

### Console Commands

- `pwmduty` - *TODO* add description.
- `gpioset` - For GPIO based LEDs, this command lets you directly change the
  state of the LED.
- `gpioget` - For GPIO based LEDs, this reads current state of the pin. If the
  current state does not track changes made with `gpioset`, check your board for
  stuck at high or stuck at low condition.

If you're having problems with a PWM LED, try reconfiguring the pin as a GPIO to
verify the board operation independent of the PWM module.

## LED Driver Chips

LED driver chips are used to control the LCD panel backlight. The backlight
control is separate from the platform LEDs.

[config.h]: ../new_board_checklist.md#config_h
[./include/ec_commands.h]: ../../include/ec_commands.h