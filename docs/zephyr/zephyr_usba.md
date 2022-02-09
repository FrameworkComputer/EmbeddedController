# Zephyr USBA Configuration

[TOC]

## Overview

[USBA] is used to configure the number of USB Type-A ports in the system and
optional control of the power supplied by said ports.

## Kconfig Options

`CONFIG_PLATFORM_EC_USBA` enables support for USB-A ports in the EC application.
Refer to [Kconfig.usba] for all sub-options controlling USB-A ports.

## Devicetree Nodes

By default, for each USB Type-A port, a GPIO pin is required to control when power
is supplied to the port. The GPIO pins are described in Device Tree nodes.

Refer to the [named-gpios.yaml] and [cros-ec,usba-port-enable-pins.yaml] child-binding
files for details about gpio properties.

## Board Specific Code

none

## Threads

When `CONFIG_PLATFORM_EC_USB_PORT_POWER_DUMB=y`, then the EC application automatically
powers up USB-A ports when the AP chipset starts up and powers off the USB-A ports
when the AP chipset shuts down.

## Testing and Debugging

Use the  gpioset  console command to manually enable and disable the USB Type-A port power.

`gpioset` usage: gpioset <pin_name> <0 | 1>

The `usbchargemode` console command is used to enable and disable charging
from the USB Type-A port.

* For dumb power ports: `usbchargemode` <port>  <on | off>
* For smart power ports: `usbchargemode` <port> <0 | 1 | 2 | 3> <0 | 1>

Charging from USB Type-A ports can be controlled from the AP using  ectool `usbchargemode`.

`ectool usbchargemode` <port> <disabled | SDP | CDP | DCP> [inhibit_charge]

Refer to the Application Processor to EC communication for more information about using[ ectool].

## Example

The Herobrine board has one USB Type-A port:

The following configures the project for one port.

```
`CONFIG_PLATFORM_EC_USB_PORT_POWER_DUMB=y`
```

The following device tree node configures the gpio pin.

```
gpio_en_usb_a_5v: en_usb_a_5v {
	gpios = <&gpiof 0 GPIO_OUT_LOW>;
};

usba-port-enable-list {
	compatible = "cros-ec,usba-port-enable-pins";
	enable-pins = <&gpio_en_usb_a_5v>;
};
```

[USBA]: ../ec_terms.md#usba
[Kconfig.usba]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.usba
[named-gpios.yaml]: ../../zephyr/dts/bindings/gpio/named-gpios.yaml
[cros-ec,usba-port-enable-pins.yaml]: ../../zephyr/dts/bindings/gpio/cros-ec,usba-port-enable-pins.yaml
[ectool]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/util/ectool.c;?q=function:ms_help&ss=chromiumos
