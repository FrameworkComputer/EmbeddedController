# Zephyr EC PPC Configuration

[TOC]

## Overview

Enable support for a [USB-C] [PPC].

## Kconfig Options

The Kconfig option `CONFIG_PLATFORM_EC_USBC_PPC` enables the selection of a [PPC].
See the file [Kconfig.ppc] for all Kconfig options related to this feature.

## Devicetree Nodes

The `PPC` device tree nodes are defined in the [`DTS Bindings`] file for
each type of `PPC` that extends [ppc-chip.yaml]. The `PPC` device is added
to the corresponding I2C bus node and the "named-usbc-port" contains a
phandle to the `PPC` device.

## Board Specific Code

None required.

## Threads

PPC support does not enable any threads.

## Testing and Debugging

The [`I2C bus scan`] can be used to verify the PPC device can be accessed and
the `ppc_dump` console command can be used to dump the PPC register.

```
Usage: ppc_dump <USB-C port>
```

## Example

The Hoglin system uses the Silergy SYV682X PPC on USBC port 0.

```
CONFIG_PLATFORM_EC_USBC_PPC=y
CONFIG_PLATFORM_EC_USBC_PPC_SYV682X=y
```

```
port0@0 {
	compatible = "named-usbc-port";
	reg = <0>;
	ppc = <&ppc_port1>;
};

&i2c2_0 {
	ppc_port1: syv682x@41 {
		compatible = "silergy,syv682x";
		status = "okay";
		reg = <0x41>;
		frs_en_gpio = <&gpio_usb_c1_frs_en>;
	};
};
```

[USB-C]: ../usb-c.md
[PPC]: ../ec_terms.md#ppc
[`I2C bus scan`]: ./zephyr_i2c.md#Shell-Command_i2c
[Kconfig.ppc]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.ppc
[`DTS Bindings`]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/dts/bindings/usbc/ppc
[ppc-chip.yaml]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/dts/bindings/usbc/ppc-chip.yaml
