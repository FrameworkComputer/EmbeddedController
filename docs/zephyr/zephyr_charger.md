# Zephyr EC Charger

[TOC]

## Overview

The charger chip enables an external power supply to provide power to the
board's components.

## Kconfig Options

`CONFIG_PLATFORM_EC_CHARGER` enables charging support in the EC
application. Refer to [Kconfig.charger] for all sub-options controlling charging
behavior.

Note: At least one charger IC must be enabled.

Note: The charger chip configuration serves a different role than the USB
charging configuration found in [Kconfig.usb_charger].

### Example of enabled configs

In `ec/zephyr/projects/{project}/{board}/prj.conf`, one may add:

```
# Charger
CONFIG_PLATFORM_EC_CHARGER=y
CONFIG_EC_CHARGER_ISL9237=y
# Charger sub-options
CONFIG_PLATFORM_EC_CHARGE_RAMP_HW=y
CONFIG_PLATFORM_EC_CHARGER_DISCHARGE_ON_AC=y
CONFIG_PLATFORM_EC_CHARGER_DISCHARGE_ON_AC_CHARGER=y
CONFIG_PLATFORM_EC_CHARGER_MIN_BAT_PCT_FOR_POWER_ON=2
CONFIG_PLATFORM_EC_CHARGER_MIN_POWER_MW_FOR_POWER_ON=10000
CONFIG_PLATFORM_EC_CHARGER_PROFILE_OVERRIDE=y
CONFIG_PLATFORM_EC_CHARGER_PSYS=y
CONFIG_PLATFORM_EC_CHARGER_PSYS_READ=y
CONFIG_PLATFORM_EC_CHARGER_SENSE_RESISTOR=10
CONFIG_PLATFORM_EC_CHARGER_SENSE_RESISTOR_AC=20
CONFIG_PLATFORM_EC_CONSOLE_CMD_CHARGER_ADC_AMON_BMON=y
```

## Devicetree Nodes
### How to add a charger devicetree node

#### Add charger node as child of `named-i2c-port`

Example:

```
named-i2c-ports {
   	compatible = "named-i2c-ports";
   	charger {
   		i2c-port = {ASSOCIATED PHANDLE};
		/* Could be any name, but must correlate to "Board Specific Code" */
		/* TODO(b/228237412): Update this comment once charger chg_chips[] is
		 * created by a shim driver.
		 */
   		enum-name = "I2C_PORT_CHARGER";
   	};
};
```

See the I2C doc on [mapping legacy I2C port numbers to Zephyr devicetree nodes]
for more information on configuring the `named-i2c-port` node.

### How to create a new charger

Add `vendor,part.yaml` to the [charger bindings directory]:

Example Template:

```
description: Vendor Part Charger IC

compatible: "vendor,part"

include: i2c-device.yaml

```
See the [I2C doc](./zephyr_i2c.md) for more information on configuring I2C
device bindings.

## Board Specific Code

### Define or append to the charger\_config\_t global array

Example configuring an ISL923x charger chip:

```c
const struct charger_config_t chg_chips[] = {
	{
		/* .i2c_port must match corresponding named-i2c-port child */
		.i2c_port = I2C_PORT_CHARGER,

		/* these may vary by vendor and part */
		.i2c_addr_flags = ISL923X_ADDR_FLAGS,
		.drv = &isl923x_drv,
	},
};
```

<!-- TODO(b/228237412) - charger chips should be defined in code via DT
macros. -->

## Threads

Enabling `CONFIG_PLATFORM_EC_CHARGER` also enables the [charger thread]
described by [Kconfig.tasks].

## Testing and Debugging a Flashed Board

### EC Console Commands

Use the `charger` [EC console command] to inspect the the chip's details and
status.

Example output of `uart:~$ charger`:

```
charger
  Name:   ISL9241
  Option: 10100000110000000000000100 (0x2830004)
  Man id: 0x0049
  Dev id: 0x000e
  V_batt: 13152 (  64 - 18304,   8)
  I_batt:  2364 (   4 -  6140,   4)
  I_in:    3000 (   4 -  6140,   4)
  I_dptf: disabled
```

#### taskinfo

Use the `taskinfo` [EC console command] to inspect if the charging task was
enabled.

Example output of `uart:~$ taskinfo`:

```
 Task Ready Name         Events      Time (s)  StkUsed
 0 R << idle >>       00000000 2055.869084   80/672
 1   HOOKS            00000000    5.215864  560/800
 2   CHG_RAMP         00000000    0.108705  424/672
 3   USB_CHG_P0       00000000    0.002139  368/672
 4   USB_CHG_P1       00000000    0.002132  368/672
 5   CHARGER          00000000   17.692626  488/928
 6 R MOTIONSENSE      80000002   50.203370  632/928
 7   KEYPROTO         00000000    0.008531  312/672
 8   CHIPSET          00000000    0.026394  528/800
 9   HOSTCMD          00000000    1.483327  600/800
10 R CONSOLE          00000000    0.101999  448/928
11   POWERBTN         00000000    0.001535  464/800
12   KEYSCAN          00000000    1.144058  328/672
13   PD_C0            00000000  176.510938  632/928
14   PD_C1            00000000   73.909944  624/928
15   PD_INT_C0        00000000    0.003969  472/672
16   PD_INT_C1        00000000    0.025180  512/672
```

#### pwr_avg

Use the [pwr_avg] [EC console command] to see battery charging rates.

#### chgstate

Use the [chgstate] [EC console command] to debug and manipulate machine charging
state.

#### chgoverride

Use the `chgoverride` [EC console command] to disable or force charging from a
specific charging enabled port.
See [Kconfig.charger] for configuring multiple charging ports.

Use the [chgsup] command to view changes.

#### chglim

Use the `chglim` [EC console command] to set a max charger IC current and
voltage charging limit.

Use the [chgstate] command to view changes.

#### chgsup

Use the `chgsup` [EC console command] to get the status of the port that is the
chosen charge supplier.

Example output of `uart:~$ chgsup`:

```
port=1, type=0, cur=3000mA, vtg=15000mV, lsm=1
```

### AP Console Commands (ectool)

#### chargecurrentlimit

Use the `chargecurrentlimit` [ectool] command to set the charge limit in
milliamps.

Usage output of `uart: # ectool chargecurrentlimit`:

```
Usage: chargecurrentlimit <max_current_mA>
```

Use the [chargestate] [ectool] command to view changes.

#### chargecontrol

Use the `chargecontrol` [ectool] command to set whether the board is idle,
discharging, or resume normal operation.

Usage output of `uart: # ectool chargecontrol`:

```
Usage: chargecontrol <normal | idle | discharge>
```

Use the [chargestate] [ectool] command to view changes.

#### chargeoverride

Use the `chargeoverride` [ectool] command to disable or force charging from a
specific charging enabled port.
See [Kconfig.charger] for configuring multiple charging ports.

Usage output of `uart: # ectool chargeoverride`:

```
Usage: chargeoverride <port# | dontcharge | off>
```

Use the `power_supply_info` [developer console] command to view which port is
acting as the charge supplier.

Example:

Usage output of `uart: # power_supply_info`:

```
power_supply_info
Device: Line Power
  path:                    /sys/class/power_supply/CROS_USBPD_CHARGER1
  online:                  yes
  type:                    USB
  enum type:               USB
  voltage (V):             4.512
  current (A):             Not available
  max voltage (V):         5
  max current (A):         3
  active source:           CROS_USBPD_CHARGER1 /* Port 1 is the charge supplier*/
  available sources:       CROS_USBPD_CHARGER0* [0/0], CROS_USBPD_CHARGER1* [0/0]
  supports dual-role:      yes
Device: Battery
  path:                    /sys/class/power_supply/BAT0
  vendor:                  AS3GXAE
  model name:              C536-49
  state:                   Fully charged
  voltage (V):             13
  energy (Wh):             52.771
  energy rate (W):         0
  current (A):             0
  charge (Ah):             4.442
  full charge (Ah):        4.442
  full charge design (Ah): 4.8
  percentage:              100
  display percentage:      100
  technology:              Li-ion

```

#### chargestate

The [chargestate] command may also be invoked.

<!-- Reference Links -->
[EC console command]: https://chromium.googlesource.com/chromiumos/platform/ec/+/HEAD/README.md#useful-ec-console-commands
[Kconfig.charger]: https://crsrc.org/o/src/platform/ec/zephyr/Kconfig.charger
[Kconfig.tasks]: https://crsrc.org/o/src/platform/ec/zephyr/Kconfig.tasks?q=%22config%20HAS_TASK_CHARGER%22&ss=chromiumos
[Kconfig.usb_charger]: https://crsrc.org/o/src/platform/ec/zephyr/Kconfig.usb_charger?q=%22config%20PLATFORM_EC_USB_CHARGER%22&ss=chromiumos
[charger bindings directory]: https://crsrc.org/o/src/platform/ec/zephyr/dts/bindings/charger/
[charger thread]: https://crsrc.org/o/src/platform/ec/common/charge_state_v2.c?q=%22void%20charger_task%22&ss=chromiumos
[chargestate]: ./zephyr_battery.md#chargestate
[chgstate]: ./zephyr_battery.md#chgstate
[chgsup]: #chgsup
[developer console]: https://chromium.googlesource.com/chromiumos/platform2/+/HEAD/crosh#crosh-the-chromium-os-shell
[ectool]: ../docs/ap-ec-comm.md
[mapping legacy I2C port numbers to Zephyr devicetree nodes]: ./zephyr_i2c.md#mapping-legacy-i2c-port-numbers-to-zephyr-devicetree-nodes
[pwr_avg]: ./zephyr_battery.md#pwr_avg
