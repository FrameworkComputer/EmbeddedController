# Zephyr EC LEDs

[TOC]

## Overview

[LEDs](../ec_terms.md#led) provide status about the following:

-   Dedicated battery state/charging state
-   Chromebook power
-   Adapter power
-   Left side USB-C port (battery state/charging state)
-   Right side USB-C port (battery state/charging state)
-   Recovery mode
-   Debug mode

LEDs can be configured as simple GPIOs, with on/off control only, or as [PWM](../ec_terms.md#pwm) with
adjustment brightness and color.

## Kconfig Options

The `CONFIG_PLATFORM_EC_LED_DT` option, found in the [Kconfig.led_dt](../../zephyr/Kconfig.led_dt) file, enables devicetree based configuration of LED
policies and colors.

Enabling the devicetree LED implementation requires that you disable the legacy EC implementation.

Example:
```
# LED
CONFIG_PLATFORM_EC_LED_COMMON=n
CONFIG_PLATFORM_EC_LED_DT=y
```

Enable other [config options](../configuration/leds.md) supported in the legacy code.

## Devicetree Nodes

LEDs are configured in two steps.

### Configure LED colors
The LED colors are configured using either GPIO based LEDs or PWM based LEDs.

#### GPIO based LEDs
Configure GPIO based LEDs using `cros-ec,gpio-led-pins` compatible node described by [cros-ec,gpio_led_pins.yaml].

Example:

For this example, the board contains dual-channel LED, one channel turns on the blue color, and one channel turns on the amber color.
To set the LED color to amber, the yellow channel is enabled and the blue channel is disabled.

```
gpio-led-pins {
	compatible = "cros-ec,gpio-led-pins";
        /* Amber - turn on yellow LED */
	color_amber: color-amber {
		led-pins = <&gpio_ec_chg_led_y_c1 1>,
			   <&gpio_ec_chg_led_b_c1 0>;
	};
	/* Blue - turn on blue LED */
	color_blue: color-blue {
		led-pins = <&gpio_ec_chg_led_y_c1 0>,
			   <&gpio_ec_chg_led_b_c1 1>;
	};
	/* White - turn on both LEDs */
	color_white: color-white {
		led-pins = <&gpio_ec_chg_led_y_c1 1>,
			   <&gpio_ec_chg_led_b_c1 1>;
	};
	/* Off - turn off both LEDs */
	color_off: color-off {
		led-pins = <&gpio_ec_chg_led_y_c1 0>,
			   <&gpio_ec_chg_led_b_c1 0>;
	};
};
```
GPIO LED Pins dts file example: [led_pins_herobrine.dts]

#### PWM based LEDs
Configure PWM based LEDs with two separate nodes.
The `cros-ec,pwm-pin-config` node, described in [cros-ec,pwm_led_pin_config.yaml], configures the PWM channel and frequency.
The `cros-ec,pwm-led-pins` node, described in [cros-ec,pwm_led_pins.yaml], configures the LED colors.
PWM LEDs can vary duty-cycle percentage, providing finer color control over GPIO LEDs.

Example:

For this example, the board contains dual-channel LED, one channel controls white color intensity, and one channel controls the amber color intensity.
To set the LED color to amber, the yellow channel duty-cycle is set to 100 percentage and white channel duty-cycle is set to 0.
```
pwm_pins {
	compatible = "cros-ec,pwm-pin-config";

	pwm_y: pwm_y {
		#led-pin-cells = <1>;
		pwms = <&pwm2 0 PWM_HZ(100) PWM_POLARITY_INVERTED>;
	};

	pwm_w: pwm_w {
		#led-pin-cells = <1>;
		pwms = <&pwm3 0 PWM_HZ(100) PWM_POLARITY_INVERTED>;
	};
};

pwm-led-pins {
	compatible = "cros-ec,pwm-led-pins";
	pwm-frequency = <100>;
	/* Amber - turn on yellow LED */
	color_amber: color-amber {
		led-pins = <&pwm_y 100>,
			   <&pwm_w 0>;
	};
	/* White - turn on white LED */
	color_white: color-white {
		led-pins = <&pwm_y 0>,
			   <&pwm_w 100>;
	};
	/* Off - turn off both LEDs */
	color_off: color-off {
		led-pins = <&pwm_y 0>,
			   <&pwm_w 0>;
	};
};
```

PWM LED Pins dts file example: [led_pins_skyrim.dts]

### Configure LED Policies
`cros-ec,led-policy` nodes describe the LED policy and set the LED behavior by referencing `cros-ec,gpio-led-pins` or `cros-ec,pwm-led-pins` nodes.
These are described in [cros-ec,led_policy.yaml]


Example:

Color policies to configure physical behavior of an LED

e.g. If you want an LED to blink, create 2 color policies, one to turn on the LED and one to turn off the LED.

```
color-0 {
	led-color = <&color_amber>;
        period-ms = <1000>;
        };
color-1 {
	led-color = <&color-off>;
        period-ms = <1000>;
        };
```

To tie this behavior with a system state, properties defining system state and color policies are added to `cros-ec,led-policy` node.

e.g. To add a blinking behavior for a system state where charge-state is "PWR_STATE_DISCHARGE and chipset-state is "POWER_S3", a policy node
is defined as below.

```
led-policy {
	compatible = "cros-ec,led-policy";
	...
	...
	power-state-discharge-s3 {
		charge-state = "PWR_STATE_DISCHARGE";
		chipset-state = "POWER_S3";

		/* Amber 1 sec, off 3 sec */
		color-0 {
			led-color = <&color_amber>;
			period-ms = <1000>;
		};
		color-1 {
			led-color = <&color_off>;
			period-ms = <3000>;
		};
	};
	...
	...
}
```

Note: It is recommended to split the policy specification and the pins specification into two devicetree files. e.g. [led_policy_skyrim.dts],  [led_pins_skyrim.dts]

LED policy dts file examples
[led_policy_skyrim.dts], [led_policy_herobrine.dts]

## Board Specific Code

None

## Threads

The LEDs are controlled by hook task in the file [led_driver/led.c](https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/shim/src/led_driver/led.c).

## Testing and Debugging
TODO: Enable support for ledtest

## Examples

### How to setup LEDs and add nodes

![Alt text](https://screenshot.googleplex.com/4eqXmo2jLcSD6eL.png)

-   Look for the gpio/pwm pins in the schematic with which the LEDs are attached to.
-   In the above snippet, LEDs are configured to use PWM pins and attached to PWM2 and PWM3.
-   Add PWM config nodes as shown in [cros-ec,pwm_led_pin_config.yaml] and [led_pins_skyrim.dts].
-   Add pin nodes based on the color of the LEDs attached as shown in [cros-ec,pwm_led_pins.yaml] and [led_pins_skyrim.dts]. Name the nodes according to the LED color for readability. e.g. `color-amber`
-   Based on the device LED policy, create led_policy nodes as shown in [cros-ec,led_policy.yaml] and [led_policy_skyrim.dts].

### PWM

[Example CL enabling single port pwm based LEDs]

### GPIO

[Example CL enabling dual port gpio based LEDs]

<!-- Reference Links -->
[cros-ec,led_policy.yaml]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/dts/bindings/leds/cros-ec,led-colors.yaml
[cros-ec,gpio_led_pins.yaml]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/dts/bindings/leds/cros-ec,gpio-led-pins.yaml
[cros-ec,pwm_led_pins.yaml]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/dts/bindings/leds/cros-ec,pwm-led-pins.yaml
[cros-ec,pwm_led_pin_config.yaml]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/dts/bindings/leds/cros-ec,pwm-led-pin-config.yaml
[led_policy_skyrim.dts]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/projects/skyrim/led_policy_skyrim.dts
[led_pins_skyrim.dts]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/projects/skyrim/led_pins_skyrim.dts
[led_policy_herobrine.dts]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/projects/herobrine/led_policy_herobrine.dts
[led_pins_herobrine.dts]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/projects/herobrine/led_pins_herobrine.dts
[Example CL enabling single port pwm based LEDs]: https://chromium-review.googlesource.com/c/chromiumos/platform/ec/+/3651490
[Example CL enabling dual port gpio based LEDs]: https://chromium-review.googlesource.com/c/chromiumos/platform/ec/+/3635067
