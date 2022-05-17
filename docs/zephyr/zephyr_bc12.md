# Zephyr BC12 Configuration

[TOC]

## Overview

The Battery Charging Specification 1.2 describes mechanisms which allow drawing
current in excess of the USB 2.0 specification. In the Zephyr EC, BC1.2 refers
to devices known as USB Charger Detectors, which allow detecting when a BC1.2
compliant charger is connected.

## Kconfig Options

The Kconfig option [`CONFIG_PLATFORM_EC_USB_CHARGER`] enables BC1.2 support in
the EC application. This option is enabled by default when
[`CONFIG_PLATFORM_EC_USBC`] is enabled. Refer to [Kconfig.usb_charger] for all
sub-options related to BC1.2 support.

## Devicetree Nodes

A BC1.2 device node should be child of an USBC port node with a compatible
property equals to "named-usbc-port". The USBC port node should have only one
BC1.2 device node.

### Richtek RT1739

There are two nodes describing the Richtek RT1739, one for BC1.2
[richtek,rt1739-bc12.yaml] and one for PPC [richtek,rt1739-ppc.yaml]. The node
for the PCC contains information about I2C bus and address.

### Richtek RT9490

The Richtek RT9490 is described by [richtek,rt9490-bc12.yaml]. It allows
defining which GPIO receives interrupts from the RT9490.

### Pericom PI3USB9201

The Pericom PI3USB9201 is described by [pericom,pi3usb9201.yaml]. It allows
defining which GPIO receives interrupts from the PI3USB9201. The DTS node
contains information about I2C bus and address.

## Board Specific Code

Enabling [CONFIG_PLATFORM_EC_USB_PD_5V_EN_CUSTOM] requires the board to provide
a custom `battery_is_sourcing_vbus()` function.

Some boards may implement the [board_vbus_sink_enable()] function.

## Threads

When enabled, a `usb_charger_task` will be created for each USBC port.
The task's stack size can be set using CONFIG_TASK_USB_CHG_STACK_SIZE, but
the priority is fixed. Additional information about the task priority can be
found in [shimmed_task_id.h].

## Example

Example of defining a BC1.2 chip in DTS:

```
named-i2c-ports {
    compatible = "named-i2c-ports";
    ...
    c0_bc12: c0_bc12 {
        i2c-port = <&i2c0_0>;
        enum-name = "I2C_PORT_USB_C0_BC12";
    };
};

gpio-interrupts {
    compatible = "cros-ec,gpio-interrupts"
    int_usb_c0_bc12: usb_c0_bc12 {
        irq-pin = <&gpio_usb_c0_bc12_int_odl>;
        flags = <GPIO_INT_EDGE_FALLING>;
        handler = "bc12_interrupt";
    };
};

port0@0 {
    compatible = "named-usbc-port";
    reg = <0>;
    bc12 {
        compatible = "pericom,pi3usb9201";
        status = "okay";
        irq = <&int_usb_c0_bc12>;
        port = <&c0_bc12>;
        i2c-addr-flags = "PI3USB9201_I2C_ADDR_3_FLAGS";
    };
};
```

`bc12` is a BC1.2 device node ("pericom,pi3usb9201" is a compatible that is
used by one of the BC1.2 devices). The `bc12` is child of the `port0@0` which
has to be the "named-usbc-port". Each "named-usbc-port" can have no more than
one BC1.2 device node.

[Kconfig.usb_charger]: https://source.chromium.org/chromium/chromiumos/platform/ec/+/HEAD:zephyr/Kconfig.usb_charger
[richtek,rt1739-bc12.yaml]: https://source.chromium.org/chromium/chromiumos/platform/ec/+/HEAD:zephyr/dts/bindings/usbc/richtek,rt1739-bc12.yaml
[richtek,rt1739-ppc.yaml]: https://source.chromium.org/chromium/chromiumos/platform/ec/+/HEAD:zephyr/dts/bindings/usbc/richtek,rt1739-ppc.yaml
[richtek,rt9490-bc12.yaml]: https://source.chromium.org/chromium/chromiumos/platform/ec/+/HEAD:zephyr/dts/bindings/usbc/richtek,rt9490-bc12.yaml
[pericom,pi3usb9201.yaml]: https://source.chromium.org/chromium/chromiumos/platform/ec/+/HEAD:zephyr/dts/bindings/usbc/pericom,pi3usb9201.yaml
[shimmed_task_id.h]: https://source.chromium.org/chromium/chromiumos/platform/ec/+/HEAD:zephyr/shim/include/shimmed_task_id.h
[CONFIG_PLATFORM_EC_USB_PD_5V_EN_CUSTOM]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.pd?q=%22PLATFORM_EC_USB_PD_5V_EN_CUSTOM%22
[board_vbus_sink_enable()]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/include/usb_charge.h?q=%22board_vbus_sink_enable%22
