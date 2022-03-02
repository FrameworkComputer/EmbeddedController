# Zephyr EC GPIO Configuration

[TOC]

## Overview

[GPIO] provide support for general purpose I/Os on the platform, including
initialization, setting and interrupt management.

## Kconfig Options

Kconfig Option                          | Default | Documentation
:-------------------------------------- | :-----: | :------------
`CONFIG_GPIO`                           | n       | [Include GPIO drivers in system config]
`CONFIG_PLATFORM_EC_GPIO_INIT_PRIORITY` | 51      | [GPIO Init Priority]

*No sub-options available.*

## Devicetree Nodes

Configure the GPIO module by declaring all GPIOs as child nodes in
the devicetree node with
`compatible` property `named-gpios`. The GPIO module automatically initializes
all GPIOs from this node (unless the `no-auto-init` property is present).
Legacy C source code accesses GPIOs using the specified
`enum-name` property as an enum name of the GPIO.
Zephyr based code uses the node label, an alias, or other node reference
to identify the GPIO.

Named GPIO properties:

Property | Description | Settings
:------- | :---------- | :-------
`gpios` | GPIO `phandle-array`, identifies the port (X), pin number (Y) and flags. | `<&gpioX Y flags>`
`enum-name` | An optional name used to define an enum to refer to the GPIO in legacy code. | `GPIO_<NAME>`
`no-auto-init` | If present, the GPIO **will not** be initialized at start-up. | boolean, default false

The use of `no-auto-init` allows GPIOs to be skipped at start-up
initialization time, and selectively enabled by code at some later time.

The file [gpio-enum-name.yaml] defines the list of valid `enum-name` values.

In the GPIO declaration use the lowercase net name from the schematic as the
*node name*, and the same net name prefixed with `gpio_` as *node label*.
For example:

```
named-gpios {
        compatible = "named-gpios";
...
        gpio_en_pp5000_fan: en_pp5000_fan {
                gpios = <&gpio6 1 GPIO_OUT_LOW>;
                enum-name = "GPIO_EN_PP5000_FAN";
        };
        gpio_power_on_odl: power_on {
                gpios = <&gpio4 4 GPIO_INPUT_PULL_UP>;
        };
};
...
aliases {
	gpio-power = &gpio_power_on_odl;
};

```

The `flags` cell of the `gpios` property defines the GPIO signal properties,
valid options are listed in [dt-bindings/gpio_defines.h], which is normally
included from the main board DTS file.

For platform specific features, other flags may be available in the Zephyr
[dt-bindings/gpio/gpio.h] file, such as `GPIO_VOLTAGE_1P8`.

### Legacy enum-name usage

Only GPIOs that require referencing from legacy common code should have
an `enum-name` property.
The legacy API (e.g `gpio_get_level(enum gpio_signal)`) requires a known
name for the enum, which is set using the `enum-name` property.

Do *not* use `enum gpio_signal` or the enum signal names in any Zephyr
based code - instead, use the standard Zephyr GPIO API.

### Zephyr GPIO API usage

GPIOs references that are not in legacy common code should use the
[standard Zephyr API](https://docs.zephyrproject.org/latest/reference/peripherals/gpio.html)
to access the GPIO.

GPIOs are referenced in the `named-gpios` child nodes using the
node label (if one exists), an alias to a node label, or
indirectly as a node reference via as a `phandle` in another node.

To facilitate this, all GPIO child nodes in `named-gpios`
have preinitialised `const struct gpio_dt_spec *` pointers
created that may be used directly in the Zephyr GPIO API calls.
These pointers are accessible via the following macros:

Macro | Argument | Description
:------- | :---------- | :-------
`GPIO_DT_FROM_NODELABEL` | nodelabel | Uses a node label to reference the GPIO node.
`GPIO_DT_FROM_NODE` | node | Uses a node id (referenced as a `phandle` in another node).
`GPIO_DT_FROM_ALIAS` | alias | Uses an alias to a label on the GPIO node.

The legacy enum can also be used to retrieve the `gpio_dt_spec` for
a GPIO via the function `gpio_get_dt_spec()` (though this is a
runtime lookup). E.g:

```
    fan_status = gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_en_pp5000_fan));
    power_status = gpio_pin_get_dt(GPIO_DT_FROM_ALIAS(gpio_power));
...
    /*
     * Legacy code gave us an enum gpio_signal, get a Zephyr reference
     * for that GPIO.
     */
    const struct gpio_dt_spec *my_gpio = gpio_get_dt_spec(my_signal);
    my_status = gpio_pin_get_dt(my_gpio);
```

The `GPIO_DT_FROM_NODE` macro is used when a `named-gpio` is referenced
from another node via a `phandle` property.

```
    gpio-interrupts {
	compatible = "cros-ec,gpio-interrupts";

	int_power_button: power_button {
	    irq-pin = <&gpio_gsc_ec_pwr_btn_odl>;
	    ...
	};
...
    /*
     * Get the GPIO associated with interrupt.
     */
    const struct gpio_dt_spec *pwr_btn =
	GPIO_DT_FROM_NODE(DT_PHANDLE(DT_NODELABEL(int_power_button), irq_pin));
    pwr_on = gpio_pin_get_dt(pwr_btn);

```

When referencing a named-gpio child node from another DTS node,
it is important not to use `gpio` or `gpios` as the trailing suffix
of the name property. Any referencing property with a name
ending in `gpio` or `gpios` is
[treated specially in devicetree](https://docs.zephyrproject.org/latest/guides/dts/bindings.html#specifier-cell-names-cells),
and assumes the target node is a GPIO node (i.e a node
with cell specifiers of `pin` and `flags`).

The goal is to migrate away from using the legacy API
(using the Zephyr API instead), and deprecate the use of the `enum-name`
property to generate the GPIO signal enum.

### Run-time configuration of GPIOs

It is common to have different hardware configurations supported
within the same EC image by using `FW_CONFIG` configuration bits
to selectively choose or enable/disable hardware options.
Previously, GPIOs were aliased via a #define in `gpio_map.h` to a common
GPIO in `named-gpios`, allowing different names to be used for the same GPIO.
At run-time the GPIO would be configured according to the usage required.

However this scheme relies on the use of the legacy
`enum gpio_signal` to identify the GPIO.
Given that code is being migrated to the Zephyr API, it is preferred that
a separate `named-gpio` node be allocated to each use of the GPIO in question,
and use the `no-auto-init` property to allow the initialisation only
when code requires it.

So if a board had 2 GPIOs with different use depending on a board type, the
configuration would appear:

```
	gpio_hdmi_enable: hdmi_enable{
                 gpios = <&gpio0 2 0>;
                 no-auto-init;
        };
	gpio_udb_c1_int: udb_c1_int {
                 gpios = <&gpio0 2 GPIO_PULL_UP>;
                 no-auto-init;
        };
```

The board config handling may have:

```
...
    if (board_type() == 1) {
	gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(gpio_hdmi_enable), GPIO_OUTPUT);
    } else {
	gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(gpio_usb_c1_int), GPIO_INPUT);
    }
```

Alternatively, a DTS alias may be used:

```
    gpio_alt_pin: alt_pin {
	gpios = <&gpio0 2 GPIO_PULL_UP>;
	no-auto-init;
    };
...
aliases {
    gpio-usb-c1-int = &gpio_alt_pin;
    gpio-hdmi-enable = &gpio_alt_pin;
};
...
    if (board_type() == 1) {
    	/* Use as output to enable the HDMI port */
	gpio_pin_configure_dt(GPIO_DT_FROM_ALIAS(gpio_hdmi_enable), GPIO_OUTPUT);
    } else {
	/* Use as type C port 1 interrupt */
	gpio_pin_configure_dt(GPIO_DT_FROM_ALIAS(gpio_usb_c1_int), GPIO_INPUT);
	/* enable interrupt */
	...
    }

```

Note that the alias names have a dash instead of an underscore
(because the alias name is a *property*, not a node name), but the
name is converted to lower case with underscores for code access.

### Unused GPIOs

Unused GPIOs should be listed separately in an `unused-gpio` node.  EC chip
specific code initializes all the unused GPIOs for optimum power consumption.

For example on the Volteer reference board:

```
    unused-pins {
        compatible = "unused-gpios";
        unused-gpios =
            <&gpio3 4 0>, /* Unused, default platform initialization. */
	    ...
            <&gpiob 6 GPIO_OUTPUT_LOW>; /* Explicit initialization flags. */
};
```

### Low Voltage Pins

Low voltage pins configuration depends on the specific chip family.

For Nuvoton, this is done using a [nuvoton,npcx-lvolctrl-def] devicetree node,
with a `lvol-io-pads` property listing all the pins that have to be configured
for low-voltage operation. For example:

```
def-lvol-io-list {
        compatible = "nuvoton,npcx-lvolctrl-def";
        lvol-io-pads = <
                &lvol_iob3 /* EC_I2C_SENSOR_SCL */
                &lvol_iob2 /* EC_I2C_SENSOR_SDA */
        >;
};
```

For ITE, this is done using the `GPIO_VOLTAGE_1P8` flag in the `named-gpios`
child node. For example

```
named-gpios {
        compatible = "named-gpios";
	...
        spi0_cs {
                gpios = <&gpiom 5 (GPIO_INPUT | GPIO_VOLTAGE_1P8)>;
                enum-name = "GPIO_SPI0_CS";
        };
	...
}
```

## GPIO Interrupts

GPIO interrupts are specified in a device tree node with
a `compatible` property of `cros-ec,gpio-interrupts`.

Child nodes of this single node contain the following properties:

Property | Description | Settings
:------- | :---------- | :-------
`irq-pin` | A reference via a node label to the named-gpio that is associated with this interrupt. | `<&gpio_label>`
`flags` | The GPIO [interrupt flags](https://docs.zephyrproject.org/latest/reference/peripherals/gpio.html) that define how the interrupt is generated. | `GPIO_INT_<flags>`
`handler` | The C name of the interrupt handler that handles the interrupt. | C function name.

For example:

```
gpio-interrupts {
        compatible = "cros-ec,gpio-interrupts";
...
        int_power_button: power_button {
                irq-pin = <&gpio_ec_pwr_btn_l>;
		flags = <GPIO_INT_EDGE_BOTH>;
                handler = "power_button_interrupt";
        };
...
}
```

There must only be one named node containing all of the device tree interrupt
configuration, but of course overlays may be used to add child nodes or
modify the single node.

The C handler takes one argument, the `enum signal` of the GPIO, such as:

```
void power_button_interrupt(enum gpio_signal signal)
{
	/* Process power button event */
...
}
```

This matches the function signature of the existing legacy interrupt
handlers, so no shims are required.

Interrupt handlers in Zephyr based code may need to compare
the `signal` against known GPIOs, if (for instance) there is a
common handler for events from multiple GPIOs.
Rather than using the predefined enums (which require that the GPIO
has an `enum-name` property), the macro `GPIO_SIGNAL(node_id)` may be
used to uniquely identify the signal regardless of whether
an `enum-name` property is on the GPIO e.g:

```
void button_input(enum gpio_signal signal)
{
	switch(signal) {
	case GPIO_SIGNAL(DT_NODELABEL(gpio_volume_up)):
		...
		break;
	case GPIO_SIGNAL(DT_NODELABEL(gpio_volume_down)):
		...
		break;
	case GPIO_SIGNAL(DT_NODELABEL(gpio_power_button)):
		...
		break;
	}
}
```

Before any interrupt can be received, it must be enabled.
Legacy code uses the functions `gpio_enable_interrupt(enum signal)` and
`gpio_disable_interrupt(enum signal)` functions. Do not use these in
any Zephyr based code.

Whilst it is possible to use the Zephyr GPIO interrupt API directly,
for convenience (until the deprecation of the legacy GPIO enum signal names)
interrupts can be identified via a macro and the label on the interrupt nodes,
and these can be used to enable or disable the interrupts:

```
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_power_button));
```

This avoid having to create boiler-plate callbacks as part of the interrupt setup.

For nodes that require a reference to an GPIO interrupt (such as sensor
configuration node etc.), the node can be referenced directly
using `GPIO_INT_FROM_NODE` e.g:

```
[DTS]
	sensor-irqs = <
		&int_imu
		&int_accel
		>;
[code]

#define ENABLE_SENSOR_INTS(i, id) \
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODE(DT_PHANDLE_BY_IDX(id, sensor_irqs, i)))

```

## Threads

GPIO support does not enable any threads.

## Testing and Debugging

### Shell Commands

The EC application defines two different shell commands to read and
change the state of a GPIO:

Command | Description | Usage
:------ | :---------- | :----
`gpioget` | Read the current state of a GPIO | `gpioget [name]`
`gpioset` | Change the state of a GPIO | `gpioset name <value>`

GPIO parameter summary:

Parameter | Description
:-------- | :----------
`name` | The GPIO node name as defined in the devicetree.
`value` | The requested state, `0` or `1`.

## Example

The image below shows a GPIO assignment on the Volteer reference board.

![GPIO Example]

In this example, the `EC_ENTERING_RW` line could be configured as:

Net Name | Port | Pin  | Flags
:------- | :--- | :--- | :----
EC_ENTERING_RW | GPIOE | 3 | Output, initialize low


Which translate in the devicetree node:

```
named-gpios {
        compatible = "named-gpios";
	...
        gpio_ec_entering_rw: ec_entering_rw {
                gpios = <&gpioe 3 GPIO_OUT_LOW>;
                enum-name = "GPIO_ENTERING_RW";
        };
	...
}
```

Set or get the GPIO value in the C source code directly using the `enum-name`
property `GPIO_ENTERING_RW`.

```
    gpio_set_level(GPIO_ENTERING_RW, 0);
    val = gpio_get_level(GPIO_ENTERING_RW);
```

Use the `node label` to reference the GPIO in other devicetree nodes:

```
my_node: my-node {
        compatible = "cros-ec,my-feature"
        signal-pin = <&gpio_ec_entering_rw>;
};
```

The `named-gpios` node and subnodes are normally declared in a separate
[gpio.dts] file, which is added as an overlay in the [BUILD.py] file for the
project.

[GPIO]: ../ec_terms.md#gpio
[GPIO Example]: ../images/gpio_example.png
[Include GPIO drivers in system config]: https://docs.zephyrproject.org/latest/kconfig.html#CONFIG_GPIO
[GPIO Init Priority]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.init_priority?q=%22config%20PLATFORM_EC_GPIO_INIT_PRIORITY%22&ss=chromiumos
[nuvoton,npcx-lvolctrl-def]: https://github.com/zephyrproject-rtos/zephyr/blob/main/dts/bindings/pinctrl/nuvoton,npcx-lvolctrl-def.yaml
[gpio-enum-name.yaml]: ../../zephyr/dts/bindings/gpio/gpio-enum-name.yaml
[dt-bindings/gpio/gpio.h]: https://github.com/zephyrproject-rtos/zephyr/blob/main/include/dt-bindings/gpio/gpio.h
[dt-bindings/gpio_defines.h]: ../../zephyr/include/dt-bindings/gpio_defines.h
[include/drivers/gpio.h]: https://docs.zephyrproject.org/latest/reference/peripherals/gpio.html?highlight=gpio_int_disable#api-reference
[gpio.dts]: ../../zephyr/projects/volteer/volteer/gpio.dts
[interrupts.dts]: ../../zephyr/projects/volteer/volteer/interrupts.dts
[BUILD.py]: ../../zephyr/projects/volteer/volteer/BUILD.py
