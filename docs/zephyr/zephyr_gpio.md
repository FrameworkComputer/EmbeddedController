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

Configure the GPIO module by declaring all GPIOs in the devicetree node with
`compatible` property `named-gpios`. The GPIO module automatically initializes
all GPIOs from this node. The C source code accesses GPIOs using the specified
`enum-name` property.

Named GPIO properties:

Property | Description | Settings
:------- | :---------- | :-------
`#gpio-cells` | Specifier cell count, always `<0>`, required if the node label is used in a `-gpios` property. | `<0>`
`gpios` | GPIO phandle, identifies the port (X), pin number (Y) and flags. | `<&gpioX Y flags>`
`enum-name` | The enum used to refer to the GPIO in the code. | `GPIO_<NAME>`

The file [gpio-enum-name.yaml] defines the list of valid `enum-name` values.

In the GPIO declaration use the lowercase net name from the schematic as the
*node name*, and the same net name prefixed with `gpio_` as *node label*. For example:

```
named-gpios {
        compatible = "named-gpios";
...
        gpio_en_pp5000_fan: en_pp5000_fan {
		#gpio-cells = <0>;
                gpios = <&gpio6 1 GPIO_OUT_LOW>;
                enum-name = "GPIO_EN_PP5000_FAN";
        };
...
}

```

The `flags` cell of the `gpios` property defines the GPIO signal properties,
valid options are listed in [dt-bindings/gpio_defines.h], which is normally
included from the main project DTS file.

For platform specific features, other flags may be available in the Zephyr
[dt-bindings/gpio/gpio.h] file, such as `GPIO_VOLTAGE_1P8`.

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

## Board Specific Code

Projects that use GPIO interrupts have to specify the interrupt to routine
mappings in a board specific file. This normally happens in [gpio_map.h] in
the project `include` directory, by specifying a series of `GPIO_INT` in a
`EC_CROS_GPIO_INTERRUPTS` define. For example:

```
#define EC_CROS_GPIO_INTERRUPTS                                               \
        GPIO_INT(GPIO_AC_PRESENT, GPIO_INT_EDGE_BOTH, extpower_interrupt)     \
        GPIO_INT(GPIO_LID_OPEN, GPIO_INT_EDGE_BOTH, lid_interrupt)            \
        ...
```

The format of GPIO_INT is:

`GPIO_INT(signal, flags, irq_handler)`

- `signal` is the GPIO `enum-name` defined in the `named-gpios` declaration,
  any of [gpio-enum-name.yaml].
- `flags` is a Zephyr GPIO interrupt flag, any of [include/drivers/gpio.h].
- `irq_handler` is function called when the interrupt is triggered.

## Threads

GPIO support does not enable any threads.

## Testing and Debugging

### Shell Commands

The EC application defines two different shell commands to read and change the state of a GPIO:

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
                #gpio-cells = <0>;
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
cbi_eeprom: eeprom@50 {
        compatible = "atmel,at24";
        reg = <0x50>;
        wp-gpios = <&gpio_ec_wp_l>;
};
```

The `named-gpios` node and subnodes are normally declared in a separate
[gpio.dts] file, which is added as an overlay in the [BUILD.py] file for the
project.

[GPIO]: ../ec_terms.md#gpio
[GPIO Example]: ../images/gpio_example.png
[Include GPIO drivers in system config]: https://docs.zephyrproject.org/latest/reference/kconfig/CONFIG_GPIO.html
[GPIO Init Priority]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.init_priority?q=%22config%20PLATFORM_EC_GPIO_INIT_PRIORITY%22&ss=chromiumos
[nuvoton,npcx-lvolctrl-def]: https://github.com/zephyrproject-rtos/zephyr/blob/main/dts/bindings/pinctrl/nuvoton,npcx-lvolctrl-def.yaml
[gpio-enum-name.yaml]: ../../zephyr/dts/bindings/gpio/gpio-enum-name.yaml
[dt-bindings/gpio/gpio.h]: https://github.com/zephyrproject-rtos/zephyr/blob/main/include/dt-bindings/gpio/gpio.h
[dt-bindings/gpio_defines.h]: ../../zephyr/include/dt-bindings/gpio_defines.h
[include/drivers/gpio.h]: https://docs.zephyrproject.org/latest/reference/peripherals/gpio.html?highlight=gpio_int_disable#api-reference
[gpio_map.h]: ../../zephyr/projects/trogdor/lazor/include/gpio_map.h
[gpio.dts]: ../../zephyr/projects/volteer/volteer/gpio.dts
[BUILD.py]: ../../zephyr/projects/volteer/volteer/BUILD.py
