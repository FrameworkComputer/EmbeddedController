# Zephyr EC USB-C SS MUX and Retimer Configuration

[TOC]

## Overview

Enable support for a [USB-C] [SuperSpeed Mux] and [Retimer]. See
[cros-ec,usb-mux-chain] for a description of how "mux-chains" work.

## Kconfig Options

The Kconfig option `CONFIG_PLATFORM_EC_USBC_SS_MUX` enables the selection of
a [USB-C] [SuperSpeed Mux]. See the file [Kconfig.usbc_ss_mux] for all
Kconfig options related to this feature.

You must also enable the Kconfig option for the specific muxex and retimers
used in your design. These Kconfig options are found in the [Kconfig.usb_mux]
and [Kconfig.retimer], respectively.

## Devicetree Nodes

The `USB-C SuperSpeed Mux` device tree nodes are defined in the [`DTS Bindings`]
file for each type of `USB-C SuperSpeed Mux` that extends [cros-ec,usbc-mux-tcpci].

The `Retimer` device tree nodes are defined in the [`DTS Bindings`] file for
each type of `Retimer`.

The `USB-C SuperSpeed Mux` device is added to the corresponding I2C
bus node and the "named-usbc-port" contains a phandle to the device.

## Board Specific Code

None required.

## Threads

USB-C SuperSpeed Mux and Retimer does not enable any threads.

## Testing and Debugging

The [`I2C bus scan`] can be used to verify the Retimer device can be accessed.

## Example
```
port0@0 {
	compatible = "named-usbc-port";
	reg = <0>;
	usb-mux-chain-0 {
		compatible = "cros-ec,usb-mux-chain";
		usb-muxes = <&usb_c0_bb_retimer
			     &virtual_mux_c0>;
	};
};
port0-muxes {
	virtual_mux_c0: virtual-mux-c0 {
		compatible = "cros-ec,usbc-mux-virtual";
	};
};

&i2c3_0 {
	status = "okay";
	clock-frequency = <I2C_BITRATE_FAST_PLUS>;
	pinctrl-0 = <&i2c3_0_sda_scl_gpd0_d1>;
	pinctrl-names = "default";

	usb_c0_bb_retimer: jhl8040r-c0@56 {
		compatible = "intel,jhl8040r";
		reg = <0x56>;
		int-pin = <&usb_c0_rt_int_odl>;
		reset-pin = <&usb_c0_rt_rst_odl>;
	};

	usb_c2_bb_retimer: jhl8040r-c2@57 {
		compatible = "intel,jhl8040r";
		reg = <0x57>;
		int-pin = <&usb_c2_rt_int_odl>;
		reset-pin = <&usb_c2_rt_rst_odl>;
	};
};

```
[USB-C]: ../usb-c.md
[SuperSpeed Mux]:../usb-c.md#ssmux
[Retimer]: ../ec_terms.md#retimer
[Kconfig.usbc_ss_mux]:
https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.usbc_ss_mux
[Kconfig.retimer]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.retimer
[`I2C bus scan`]: ./zephyr_i2c.md#Shell-Command_i2c
[cros-ec,usbc-mux-tcpci]:
https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/dts/bindings/usbc/mux/cros-ec,usbc-mux-tcpci.yaml
[cros-ec,usb-mux-chain]:
https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/dts/bindings/usbc/cros-ec,usb-mux-chain.yaml
[`DTS Bindings`]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/dts/bindings/usbc/mux
