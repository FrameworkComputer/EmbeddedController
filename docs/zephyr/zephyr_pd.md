# Zephyr EC PD Configuration

[TOC]

## Overview

Configure [USB-C] [PD] features that enables a port to provide power
greater than the basic 5V @ 900mA. Power can be negotiated up to
20V @ 5A.

## Kconfig Options

The `CONFIG_PLATFORM_EC_USB_POWER_DELIVERY` option enables USB-C power delivery
support on the Chromebook. See [Kconfig.pd] for sub-options related to this feature.

### Additional USB-C PD Configuration

The behavior of the USB-C PD implementation is further controlled through the
following options:

1. VBUS Measurement - See [Kconfig.pd_meas_vbus]
2. VBUS Detection - See [Kconfig.pd_vbus_detection]
3. VBUS Discharge - [Kconfig.pd_discharge]
4. Shared PD Interrupts - See [Kconfig.pd_int_shared]
5. Fast Role Swap - See [Kconfig.pd_frs]
6. Console Commands - See [Kconfig.pd_console_cmd]
7. USBC Device Type - See [Kconfig.pd_usbc_device_type]

## Devicetree

Devicetree nodes that have their compatible property set to `named-usbc-port` are used to
represent [USB-C] [PD] ports. For example, the follow two nodes represent the two [PD]
ports on Herobrine, aptly named port0 and port1.

```
port0@0 {
	compatible = "named-usbc-port";
	reg = <0>;
	bc12 = <&bc12_port0>;
	ppc = <&ppc_port0>;
	ppc_alt = <&ppc_port0_alt>;
	tcpc = <&tcpc_port0>;
	chg = <&charger>;
	usb-mux-chain-0 {
		compatible = "cros-ec,usb-mux-chain";
		usb-muxes = <&usb_mux_0>;
	};
};
usb_mux_0: usb-mux-0 {
	compatible = "parade,usbc-mux-ps8xxx";
};

port1@1 {
	compatible = "named-usbc-port";
	reg = <1>;
	bc12 = <&bc12_port1>;
	ppc = <&ppc_port1>;
	tcpc = <&tcpc_port1>;
	usb-mux-chain-1 {
		compatible = "cros-ec,usb-mux-chain";
		usb-muxes = <&usb_mux_1>;
	};
};
usb_mux_1: usb-mux-1 {
	compatible = "parade,usbc-mux-ps8xxx";
};

&i2c1_0 {
	ppc_port0: sn5s330@40 {
		compatible = "ti,sn5s330";
		status = "okay";
		reg = <0x40>;
	};

	ppc_port0_alt: syv682x@41 {
		compatible = "silergy,syv682x";
		status = "okay";
		reg = <0x41>;
		frs_en_gpio = <&gpio_usb_c0_frs_en>;
	};

	tcpc_port0: ps8xxx@b {
		compatible = "parade,ps8xxx";
		reg = <0xb>;
	};
};

&i2c2_0 {
	ppc_port1: sn5s330@40 {
		compatible = "ti,sn5s330";
		status = "okay";
		reg = <0x40>;
	};

	tcpc_port1: ps8xxx@b {
		compatible = "parade,ps8xxx";
		reg = <0xb>;
	};
};
```

Each port gets a `reg` property that is used to represent the port in the system. For example,
`reg = <0>` for port0 and `reg = <1>` for port1. Also note that the `reg` value matches the
value after the @ in the node name.

The primary use of the port node is to describe what devices are connected to each port.
For example, the `named-usbc-port` can include the following devices:
* [BC12]
* [PPC]
* [TCPC]
* [USB muxes and USB retimers]

The two ports shown above both have the same four devices, but this is not required. Board
designs may use different USB-C devices on each USB-C port.

[USB-C]:../usb-c.md
[PD]:../usb-c.md#pd
[VBUS]:../ec_terms.md#vbus
[FRS]:../ec_terms.md#frs
[BC12]:../ec_terms.md#bc12
[PPC]:../usb-c.md#ppc
[TCPC]:../usb-c.md#tcpc
[USB muxes and USB retimers]:../usb-c.md#ssmux
[Kconfig.pd]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.pd
[Kconfig.pd_int_shared]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.pd_int_shared
[Kconfig.pd_meas_vbus]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.pd_meas_vbus
[Kconfig.pd_frs]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.pd_frs
[Kconfig.pd_discharge]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.pd_discharge
[Kconfig.pd_vbus_detection]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.pd_vbus_detection
[Kconfig.pd_console_cmd]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.pd_console_cmd
[Kconfig.pd_usbc_device_type]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.pd_usbc_device_type
