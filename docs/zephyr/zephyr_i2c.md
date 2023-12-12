# Zephyr I2C Bus Configuration

[TOC]

## Overview

The [I2C] buses provide access and control to on-board peripherals, including
USB-C chips, battery, charging IC, and sensors.

## Kconfig Options

The Kconfig option [`CONFIG_I2C`] enables I2C support in the EC
application.  Refer to [Kconfig.i2c] for all sub-options related to I2C support.

The upstream Zephyr I2C driver also provides I2C shell commands with the
[`CONFIG_I2C_SHELL`] option.

## Devicetree Nodes

The EC chip disables all I2C buses by default.  Enable the I2C buses used on
your design by changing the chip-specific I2C bus `status` property to `"okay"`.

I2C bus properties:

Property | Description | Settings
:------- | :---------- | :-------
`status` | Enables or disables the I2C controller | `"okay"` <br> `"disabled"`
`label` | Override the EC chip specific label. We recommend changing the label to match the net name of the I2C bus. The label must begin with `"I2C_"`. |`"I2C_<net_name>"`
`clock-frequency` | Sets the initial I2C bus frequency in Hz. | `I2C_BITRATE_STANDARD` - 100 KHz <br> `I2C_BITRATE_FAST` - 400 KHz <br> `I2C_BITRATE_FAST_PLUS` - 1 MHz

Example enabling I2C0 and I2C3 at 100 KHz and 1 MHz, respectively.
```
&i2c0 {
        status = "okay";
        label = "I2C_BATTERY";
        clock-frequency = <I2C_BITRATE_STANDARD>;
};
&i2c3 {
        status = "okay";
        label = "I2C_USB_C0_PD";
        clock-frequency = <I2C_BITRATE_FAST_PLUS>;
};
```

### Nuvoton NPCX ECs

Nuvoton ECs use two devicetree nodes to describe the I2C buses used, an I2C
controller and an I2C port.

Nuvoton I2C [*node labels*] use the following pattern:
- I2C controller: `&i2c_ctrl<controller>`
- I2C port: `&i2c<controller>_<port>`

Where `<controller>` specifies the I2C controller number (0-7), and `<port>`
specifies the port number (0-1). You can only enable one I2C port per
controller, and not all I2C controllers support both ports.

The Nuvoton I2C port contains the standard Zephyr I2C bus properties. The
Nuvoton I2C controller contains only the `status` property.

To enable a Nuvoton I2C bus, set both the I2C controller and I2C port `status`
property to `"okay"`.Set the `clock-frequency` and `label` properties in the I2C
port as shown below:

```
&i2c_ctrl4 {
        status = "okay";
};
&i2c4_1 {
        status = "okay";
        label = "I2C_EEPROM";
        clock-frequency = <I2C_BITRATE_FAST>;
};
```

### ITE IT8xxx2 ECs

ITE ECs use a single devicetree node, `&i2c<channel>` to enable an I2C bus.
`<channel>` specifies the I2C/SMBus channel number (0-5).

```
&i2c3 {
        status = "okay";
        label = "I2C_USB_C0_PD";
        clock-frequency = <I2C_BITRATE_STANDARD>;
};
```

### Mapping legacy I2C port numbers to Zephyr devicetree nodes

The legacy I2C API for the Chromium EC application uses an enumeration (e.g.
`I2C_PORT_ACCEL`, `I2C_PORT_EEPROM`) to specify the I2C bus during transfer
operations.

The `named-i2c-ports` node creates the mapping between the legacy I2C bus
enumeration and the Zephyr I2C bus device instance.

```
named-i2c-ports {
        compatible = "named-i2c-ports";
        battery {
                i2c-port = <&i2c0_0>;
                remote-port = <0>;
                enum-names = "I2C_PORT_BATTERY";
        }
};
```

You can map multiple enumeration values to the same Zephyr I2C bus device
instance.

```
named-i2c-ports {
        compatible = "named-i2c-ports";
        battery {
                i2c-port = <&i2c0_0>;
                remote-port = <0>;
                enum-names = "I2C_PORT_BATTERY",
                	"I2C_PORT_CHARGER";
        }
};
```

Refer to the [cros-ec-i2c-port-base.yaml] child-binding file for details about
each property.

## Board Specific Code

None required.

## Threads

I2C support does not enable any threads.

## Testing and Debugging

### Shell Command: i2c
The EC application enables the the Zephyr shell command, `i2c`, when
`CONFIG_I2C_SHELL=y`. The `i2c` command includes the following [subcommands]:

Subcommand | Description | Usage
:--------- | :---------- | :----
`scan` | Scan I2C devices | `i2c scan <i2c_bus_label>`
`recover` | Recover I2C bus | `i2c recover <i2c_bus_label>`
`read` | Read bytes from an I2C device | `i2c read <i2c_bus_label> <dev_addr> <reg_addr> [<num_bytes>]`
`read_byte` | Read a byte from an I2C device | `i2c read_byte <i2c_bus_label> <dev_addr> <reg_addr>`
`write` | Write bytes to an I2C device | `i2c write <i2c_bus_label> <dev_addr> <reg_addr> <out_byte0> .. <out_byteN>`
`write_byte` | Write a byte to an I2C device | `i2c write_byte <i2c_bus_label> <dev_addr> <reg_addr> <out_byte>`

I2C parameter summary:

Parameter | Description
:-------- | :----------
`<i2c_bus_label>` | The I2C bus label property. By default this is specified by the EC vendor in the respective devicetree include file unless you override the label in your devicetree.
`<dev_addr>` | The I2C device address, specified using 7-bit notation. Valid device addresses are 0 - 0x7F.
`<reg_addr>` | The register address with the I2C device to read or write.
`<num_bytes>` | For the `read` subcommand, specifies the number of bytes to read from the I2C device. Default is 16 bytes if not specified.
`<out_byte>` | For the `write_byte` subcommand, specifies the single data byte to write to the I2C device.
`<out_byte0>..<out_byteN>` | For the `write` subcommand, specifies the data bytes to write to the I2C device.

### Shell Command: i2c_portmap
The shell command `i2c_portmap` displays the mapping of I2C bus enumeration to
the physical bus and to the remote port index.

Example `i2c_portmap` output from a Volteer board:
```
uart:~$ i2c_portmap
Zephyr physical I2C ports (9):
  0 : 0
  1 : 0
  2 : 1
  3 : 2
  4 : 3
  5 : 4
  6 : 4
  7 : 5
  8 : 5
Zephyr remote I2C ports (9):
  0 : -1
  1 : -1
  2 : -1
  3 : -1
  4 : -1
  5 : -1
  6 : -1
  7 : 7
  8 : -1
```

### I2C Tracing

For runtime troubleshooting of an I2C device, enable the [I2C
tracing](../i2c-debugging.md) module to log all I2C transactions initiated by
the EC code.

## Example

The image below shows the I2C bus assignment for the Volteer reference board.

![I2C Example]

The Volteer reference design uses the Nuvoton NPCX EC, and needs the following
I2C buses enabled:

Net Name           | NPCX I2C Designator | Bus speed
:----------------- | :------------------ | :--------
EC_I2C7_EEPROM_PWR | I2C7_PORT0          | 400 kHz
EC_I2C5_BATTERY    | I2C5_PORT0          | 100 kHz
EC_I2C0_SENSOR     | I2C0_PORT0          | 400 kHz
EC_I2C1_USB_C0     | I2C1_PORT0          | 1000 kHz
EC_I2C2_USB_C1     | I2C2_PORT0          | 1000 kHz
EC_I2C3_USB_1_MIX  | I2C3_PORT0          | 100 kHz


### Enable Nuvoton I2C buses
The Volteer project enables the Nuvoton I2C buses in [volteer.dts].

```c
&i2c0_0 {
	status = "okay";
	clock-frequency = <I2C_BITRATE_FAST>;
	label = "I2C_SENSOR";
};
&i2c_ctrl0 {
	status = "okay";
};

&i2c1_0 {
	status = "okay";
	clock-frequency = <I2C_BITRATE_FAST_PLUS>;
	label = "I2C_USB_C0";
};
&i2c_ctrl1 {
	status = "okay";
};

&i2c2_0 {
	status = "okay";
	clock-frequency = <I2C_BITRATE_FAST_PLUS>;
	label = "I2C_USB_C1";
};
&i2c_ctrl2 {
	status = "okay";
};

&i2c3_0 {
	status = "okay";
	clock-frequency = <I2C_BITRATE_STANDARD>;
	label = "I2C_USB_1_MIX";
};
&i2c_ctrl3 {
	status = "okay";
};

&i2c5_0 {
	status = "okay";
	clock-frequency = <I2C_BITRATE_STANDARD>;
	label = "I2C_BATTERY";
};
&i2c_ctrl5 {
	status = "okay";
};

&i2c7_0 {
	status = "okay";
	clock-frequency = <I2C_BITRATE_FAST>;
	label = "I2C_EEPROM_PWR";

	isl9241: isl9241@9 {
		compatible = "intersil,isl9241";
		reg = <0x09>;
		label = "ISL9241_CHARGER";
		switching-frequency = <SWITCHING_FREQ_724KHZ>;
	};
};
&i2c_ctrl7 {
	status = "okay";
};
```

### Map I2C Enumerations
The legacy cros-ec drivers require the board to define the following enumeration
values:

I2C Enumeration Name | Volteer I2C Bus Mapping
:------------------- | :----------------------
`I2C_PORT_SENSOR`    | EC_I2C0_SENSOR
`I2C_PORT_ACCEL`     | EC_I2C0_SENSOR
`I2C_PORT_USB_C0`    | EC_I2C1_USB_C0
`I2C_PORT_USB_C1`    | EC_I2C2_USB_C1
`I2C_PORT_USB_1_MIX` | EC_I2C3_USB_1_MIX
`I2C_PORT_POWER`     | EC_I2C5_BATTERY
`I2C_PORT_BATTERY`   | EC_I2C5_BATTERY
`I2C_PORT_EEPROM`    | EC_I2C7_EEPROM_PWR
`I2C_PORT_CHARGER`   | EC_I2C7_EEPROM_PWR

The Volteer project establishes this map using the `named-i2c-ports` as shown
below:

```c
	named-i2c-ports {
		compatible = "named-i2c-ports";

		i2c_sensor: sensor {
			i2c-port = <&i2c0_0>;
			enum-names = "I2C_PORT_SENSOR",
				"I2C_PORT_ACCEL";
		};
		i2c_usb_c0: usb-c0 {
			i2c-port = <&i2c1_0>;
			enum-names = "I2C_PORT_USB_C0";
		};
		i2c_usb_c1: usb-c1 {
			i2c-port = <&i2c2_0>;
			enum-names = "I2C_PORT_USB_C1";
		};
		usb1-mix {
			i2c-port = <&i2c3_0>;
			enum-names = "I2C_PORT_USB_1_MIX";
		};
		power {
			i2c-port = <&i2c5_0>;
			enum-names = "I2C_PORT_POWER",
				"I2C_PORT_BATTERY";
		};
		eeprom {
			i2c-port = <&i2c7_0>;
			enum-names = "I2C_PORT_EEPROM",
				"I2C_PORT_CHARGER";
		};
	};
```

[I2C]: ../ec_terms.md#i2c
[subcommands]: https://github.com/zephyrproject-rtos/zephyr/blob/f4a0ea7b43eee4d2ee735ab6beccc68c9d40a7d0/drivers/i2c/i2c_shell.c#L245
[I2C Example]: ../images/i2c_example.png
[Kconfig.i2c]: ../../zephyr/Kconfig.i2c
[`CONFIG_I2C`]: https://docs.zephyrproject.org/latest/kconfig.html#CONFIG_I2C
[`CONFIG_I2C_SHELL`]: https://docs.zephyrproject.org/latest/kconfig.html#CONFIG_I2C_SHELL
[cros-ec-i2c-port-base.yaml]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/dts/bindings/i2c/cros-ec-i2c-port-base.yaml
[volteer.dts]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/boards/arm/volteer/volteer.dts;
[*node labels*]: https://docs.zephyrproject.org/latest/build/dts/intro.html#dt-node-labels
