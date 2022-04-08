# Zephyr EC Battery

[TOC]

## Overview

The battery is the rechargeable internal power source for the device.

## Kconfig Options

`CONFIG_PLATFORM_EC_BATTERY` enables battery support in the EC application.
Refer to [Kconfig.battery] for all sub-options controlling battery behavior.

## Devicetree Nodes

### How to enable batteries on a board

#### Enable battery feature configs

Add battery configs to `ec/zephyr/projects/{project}/{board}/prj.conf`.

Example:

```
# Battery
CONFIG_PLATFORM_EC_BATTERY=y
CONFIG_PLATFORM_EC_BATTERY_SMART=y
CONFIG_PLATFORM_EC_BATTERY_FUEL_GAUGE=y
CONFIG_PLATFORM_EC_BATTERY_CUT_OFF=y
CONFIG_PLATFORM_EC_BATTERY_HW_PRESENT_CUSTOM=y
CONFIG_PLATFORM_EC_BATTERY_REVIVE_DISCONNECT=y

```

#### Add devicetree nodes

##### Add batteries devicetree node to the board overlay's root node

Example:

```
  batteries {
		default_battery: vendor_part {
			compatible = "vendor,part";
		};
		vendor2_part2 {
			compatible = "vendor2,part2";
		};
   };

```

Here `vendor_part` will be the default battery type. If this [*node label*] is
present in the overlay, the [DEFAULT_BATTERY_TYPE] is set in the battery shim
code with the labeled battery type. The `vendor` and `part` references must
match an existing battery defined in [battery bindings directory].

##### Add the battery present GPIO node as a child of `named-gpios`

Example:

```
named-gpios {
	compatible = "named-gpios";
	...
	...
	ec_batt_pres_odl {
		gpios = <{SOME GPIO} GPIO_INPUT>;
	};
	...
	...
};
```

See the [Zephyr gpios] doc for more information on the `named-gpios` node.

##### Add a battery node as a child of `named-i2c-ports`

Example:

```
named-i2c-ports {
	compatible = "named-i2c-ports";
	...
	...
	battery {
		i2c-port = <{i2c_phandle}>;
		remote-port = <{I2C_PASSTHRU-PORT-NUMBER}>;
		enum-name = "I2C_PORT_BATTERY";
	};
	...
	...
}
```

Most battery fuel gauges support operation of only 100 KHz on the I2C bus, so
ensure the clock-frequency for the bus is set appropriately.  See the
[Zephyr I2C] doc for more information on the `named-i2c-ports` node.

Refer to the [cros-ec-i2c-port-base.yaml] child-binding file for details about
each property.

[Example CL enabling batteries on a board]

### How to create a new battery

+ Add `vendor,part` to [battery-smart enum]
+ Add `vendor,part.yaml` to the [battery bindings directory] beginning with:

```
description: "VENDOR PART"
compatible: "vendor,part"

include: battery-smart.yaml

properties:
   enum-name:
      type: string
      default: "vendor,part"

```

Refer to the vendor's datasheet to set all the fuel gauge and battery properties
required in the `vendor,part.yaml` file.

[Example CL adding a new battery]

## Board Specific Code

Enabling [CONFIG_PLATFORM_EC_BATTERY_PRESENT_CUSTOM] requires the board to provide a
custom `battery_is_present()` function.

## Threads

Battery support alone does not enable any threads. However, the charger [task]
requires the battery module for correct operation.

## Testing and Debugging

### EC Console Commands

#### battery

The `battery` [EC console command] may be invoked to check battery information
on a flashed board.

Example output of `uart:~$ battery`:

```
Status:    0x00e0 FULL DCHG INIT
Param flags:00000002
Temp:      0x0b69 = 292.1 K (19.0 C)
V:         0x2ffc = 12284 mV
V-desired: 0x0000 = 0 mV
I:         0x0000 = 0 mA
I-desired: 0x0000 = 0 mA
Charging:  Not Allowed
Charge:    96 %
  Display: 98.7 %
Manuf:     LGC
Device:    AC17A8M
Chem:      LION
Serial:    0xd3b3
V-design:  0x2d1e = 11550 mV
Mode:      0x6001
Abs charge:73 %
Remaining: 3901 mAh
Cap-full:  4074 mAh
  Design:  5360 mAh
Time-full: 0h:0
  Empty:   0h:0
full_factor:0.97
shutdown_soc:4 %
```

#### pwr_avg

The `pwr_avg` [EC console command] logs the battery charging rate by querying
the battery fuel gauge driver.

Example output of `uart:~$ pwr_avg`:

Charging

```
mv = 13073
ma = 439
mw = 573
```

Discharging

```
mv = 12824
ma = -146
mw = -1872
```

Note: A fully charged board may report `ma = 0` and `mw = 0` average rates.

#### chgstate

The `chgstate` [EC console command] may be invoked to debug and manipulate machine
charging state.

Example output of `uart:~$ chgstate`:

```
state = charge
ac = 1
batt_is_charging = 1
chg.*:
	voltage = 13200mV
	current = 0mA
	input_current = 3000mA
	status = 0xc010
	option = 0x2830004
	flags = 0x0
batt.*:
	temperature = 26C
	state_of_charge = 100%
	voltage = 13037mV
	current = 0mA
	desired_voltage = 0mV
	desired_current = 0mA
	flags = 0x2
	remaining_capacity = 4436mAh
	full_capacity = 4436mAh
	is_present = YES
requested_voltage = 0mV
requested_current = 0mA
chg_ctl_mode = 0
manual_voltage = -1
manual_current = -1
user_current_limit = -1mA
battery_seems_to_be_dead = 0
battery_seems_to_be_disconnected = 0
battery_was_removed = 0
debug output = off
```

### AP Console Commands (ectool)

#### chargestate

The `chargestate` [ectool] command may be invoked to debug and manipulate
machine charging state.

Usage output of `uart: # ectool chargestate`:

```
Usage:
  chargestate show                  - show current state
  chargestate param NUM [VALUE]     - get/set param NUM
  chargestate param help            - show known param NUMs
```

<!-- Reference Links -->

[CONFIG_PLATFORM_EC_BATTERY_PRESENT_CUSTOM]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.battery?q=%22PLATFORM_EC_BATTERY_PRESENT_CUSTOM%22&ss=chromiumos
[DEFAULT_BATTERY_TYPE]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/shim/src/battery.c?q=%22DEFAULT_BATTERY_TYPE%22&ss=chromiumos
[EC console command]: https://chromium.googlesource.com/chromiumos/platform/ec/+/HEAD/README.md#useful-ec-console-commands
[Example CL adding a new battery]: https://chromium-review.googlesource.com/c/chromiumos/platform/ec/+/3312506/
[Example CL enabling batteries on a board]: https://chromium-review.googlesource.com/c/chromiumos/platform/ec/+/3200068/
[Kconfig.battery]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.battery
[Zephyr I2C]: zephyr_i2c.md#Mapping-legacy-I2C-port-numbers-to-Zephyr-devicetree-nodes
[Zephyr gpios]: zephyr_gpio.md#Devicetree-Nodes
[battery bindings directory]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/dts/bindings/battery/
[battery-smart enum]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/dts/bindings/battery/battery-smart.yaml?q=%22enum:%22&ss=chromiumos
[cros-ec-i2c-port-base.yaml]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/dts/bindings/i2c/cros-ec-i2c-port-base.yaml
[ectool]: ../ap-ec-comm.md
[task]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/shim/include/shimmed_task_id.h
[*node label*]: https://docs.zephyrproject.org/latest/build/dts/intro.html#dt-node-labels
