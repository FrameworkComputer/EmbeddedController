# Configuring the EC for Low-Battery Startup

Near the bottom of charge, starting up a ChromeOS device can be a tricky
proposition. Several features interact to make it difficult to reliably turn on
the machine without browning out. Over the years, a variety of configuration
options have been written to maximize ChromeOS's compatibility with the basic
user expectation,

"I plugged it in, therefore it should turn on."

When creating a new board configuration, this document should aid the engineer
in navigating and choosing correct values for these options.

The first section describes the various features which interact with each other
to create a complex environment for the EC during boot, especially at a low
state of charge.

Second, we'll provide some reference configurations which cover many
Chromebooks' use cases.

Finally, we'll close out with a detailed review of the configuration parameters
which are available.

## Interacting Features

### Battery and Charging Circuit

For the most part, ChromeOS device power systems are much like other laptop
battery power systems. A variable-voltage rail is connected to the battery via a
series of cutoff MOSFETs. Several system power rails derive their power from the
system's variable-voltage rail. Mains power is delivered to the variable-voltage
system rail by a buck/boost charging circuit. Mains power is itself rectified,
isolated, and stepped down by an external power supply.

During most of the battery charge, the charger operates in current mode, acting
as a constant current source that delivers current to the variable-voltage rail.
Load transients are served by the capacitance on the rail and the battery. By
superposition, load transients during the charge don't necessarily draw current
from the battery, they may just reduce the current flow into the battery.

References to AC power in the EC codebase are actually references to an external
power supply's DC source. External supplies that are actually USB-PD-speaking
battery packs are indistinguishable from AC/DC adapters as far as the EC is
concerned. Variables and functions which refer to external supplies all refer to
them as 'AC', though.

### Source Current Negotiation

A device may draw power from an AC adapter via a few methods.

#### USB BC1.2 Current Sources

BC1.2 negotiation is usually managed entirely by an external IC. Once it is
complete, the EC limits itself to 2.4A max. Additionally, the charger may be
configured to switch to an input voltage regulation mode if the input voltage
begins to sag too low.

Ideally, the input source provides a voltage droop, such that it is not quite
overloaded at the input voltage regulation setpoint of about 4.5V. Thus, 4.5V
serves as a reasonable reference voltage for the charger to use when it is in an
input voltage-regulation mode.

In effect, the EC limits to both a maximum current of 2.4A and minimum voltage
of 4.5V, for about 12W of power draw from a BC1.2 source.

See also `driver/bc12/max14637.c:bc12_detect()`.

#### USB-PD Sources

High-current power supplies are negotiated via the USB Type C Current Source and
USB Power Delivery specifications (PD). PD sources must support Type-C Current
Source, but the reverse is not true. Both types of current sources are managed
via the PD protocol module in the EC codebase.

Type-C Current Source capabilities of up to 15W (3A, 5V) are advertised via
analog signaling alone. Via digital communication in the PD protocol, much
higher power states may be negotiated. However, higher power states also usually
run at a higher voltage state as well. Any time the voltage level is changing,
the power sink (the ChromeOS device) must lower its power consumption during the
transient.

PD port partners are capable of both soft and hard resets. Hard resets will
cause a dead-bus state for a brief interval before PD can renegotiate, from
scratch, because it is intended to emulate a cable disconnect. Therefore, a hard
reset without a connected battery will brownout the Chromebook.

### Locked and Unlocked Firmware

The Verified Boot implementation normally limits the complexity of the code
which executes in the locked Read-Only firmware package. The consequences for
the EC are:

-   Locked RO EC firmware does not process any digital PD messages at all, it
    only recognizes the analog advertisement of USB Current Source (15W max).
-   Installation of user-provided firmware is supported, but the write-protect
    pin must be cleared to enable it.
-   On recent systems, write-protect is cleared by removing the system battery.

### ChromeOS `powerd`

The power management daemon provided by ChromeOS displays a "low-power charger"
warning message via the system tray whenever the charger is limited to less than
20W. Therefore, if a USB-PD source is restricted to analog signaling, or a BC1.2
source is connected, the user gets alerted to the situation.

Systems that can run on very little power may be rapidly charged with a 15W
charger, while a high power system may require a 40W state or more for a decent
battery charging user experience. Therefore, a board's overlay may override the
warning threshold by replacing `/usr/share/power_manager/usb_min_ac_watts` in
the board's filesystem.

See also `platform2/power_manager/` source code.

### Cell Imbalance

Under normal conditions, the battery pack is equipped with a management IC which
is solely responsible for the safety of the battery, measurement of the state of
charge, and the balance of its cells. Examples include (but are not limited to)
the TI BQ40Z50 and Renesas RAJ240.

However, after very long periods of rest without a battery charging cycle, the
natural self-discharge rate of each cell will cause them to diverge somewhat
from each other.

Some IC's can be configured to report a pack total state of charge of zero if
any one cell's voltage is below a certain threshold. However, many do not.
Therefore, after an extended rest period, one cell can be very close to the cell
undervoltage cutoff threshold, even though the pack as a whole is considered to
be at 3% charge or more.

### Power Profile During Boot

The power profile during the boot sequence is substantially different than that
seen during typical use. Dynamic voltage and frequency scaling of the AP is
partially governed by the temperature of the processor core. As the processor
gets hotter, it will reduce its maximum core voltage and frequency to settle out
at some maximum design junction temperature for the core. For passively cooled
devices, the profile may also be chosen to limit the external case temperature.

At startup, the case and core are cold. The bootloaders and kernel are also
optimized to boot as fast as possible for a responsive user experience. So, the
power drawn during the boot is much higher than that seen during typical
productivity and entertainment tasks.

### Depthcharge Power Verification

After verification and optional update of the EC's RW firwmare, Depthcharge will
poll the EC to verify that it is allowed to proceed to boot to the kernel.

It does this by polling via the: - `EC_CMD_CHARGE_STATE` host command. -
`CHARGE_STATE_CMD_GET_PARAM` subcommand. - `CS_PARAM_LIMIT_POWER` parameter.

When the EC returns 0, power draw by the AP is unlimited and depthcharge resumes
the boot. If the EC fails to return 0 in three seconds, depthcharge shuts down.

See also vb2ex_ec_vboot_done() in Depthcharge, and option
`CONFIG_CHARGER_LIMIT_POWER_THRESH_CHG_MW` in the EC. By default, this option is
not set, and the EC immediately allows the boot to proceed.

## Example Low-Battery Boot Sequences and Configurations

Most ChromeOS devices power needs will be met by one of the following templates.

### Low-Power Device

Low-power devices require 15W or less of power to boot the AP. The battery pack
is robust enough to support the device during brief intervals of PD negotiation
without browning out.

```
#define CONFIG_CHARGER_DEFAULT_CURRENT_LIMIT 512
#define CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON 1
```

A detailed boot sequence under this configuration, with a low battery and
available AC power via a USB-PD charger:

1.  EC ROM bootloader loads and jumps to the EC's read-only firmware image.
1.  RO firmware negotiates a 15W state via Current Source analog signaling and
    begins charging the battery with it.
1.  RO firmware verifies conditions to begin booting the AP:
    -   Battery state of charge > 1%
    -   OR charger power greater or equal to 15W (met by Current Source analog
        signaling).
1.  AP firmware performs verification of the EC's RW image, upgrades it if
    necessary, and sysjumps the EC to it.
1.  AP firmware queries the charge state limit power flag via EC-host command,
    and the EC immediately responds that it is clear.
1.  Depthcharge continues the boot.
    1.  In parallel with kernel loading and Linux's boot, the EC performs PD
        negotiation. Charger power lowers to 2.5W for up to 500ms as the source
        transitions from vSafe5V to its highest supported voltage (15V or 20V
        are typical). During this transition time some power is drawn from the
        battery.
    1.  After PD negotiation is complete, the EC raises the charger current
        limit to the negotiated limit (45W is typical).

### Low-Power Device Startup With Marginal Battery Compatibility

Similar in configuration to the low-power device startup, this system enables
additional options to maximize its compatibility with marginal batteries near
the bottom of charge. The Grunt family is an exemplar. This system will complete
software sync with less than 15W of power, but may require more power to boot
the kernel and get to the login screen.

```
/* Limit battery impact during PD voltage changes. */
#define CONFIG_CHARGER_DEFAULT_CURRENT_LIMIT 512

/* Distrust the battery SOC measurement a bit. */
#define CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON 3

/*
 * Require PD negotiation to be complete prior to booting Linux, but don't
 * care about how much power we negotiate.
 */
#define CONFIG_CHARGER_LIMIT_POWER_THRESH_CHG_MW 15001

/* Extra paranoia about imbalanced cells. */
#define CONFIG_BATTERY_MEASURE_IMBALANCE
```

Additionally, in order to take advantage of cell imbalance detection, the system
battery must support per-cell voltage measurement.

A detailed boot sequence under this configuration, with a low battery and
available AC power:

1.  EC ROM bootloader loads and jumps to the EC's read-only firmware image.
1.  RO firmware negotiates a 15W state via Current Source analog signaling and
    begins charging the battery with it.
1.  RO firmware verifies conditions to begin booting the AP:
    -   battery state of charge >= 3% AND cell imbalance < 200 mV
    -   OR battery state of charge >= 5%
    -   OR charger power greater or equal to 15W (met by Current Source analog
        signaling).
1.  AP firmware performs verification of the EC's RW image, upgrades it if
    necessary, and sysjumps the EC to it.
1.  AP firmware polls the charge state limit power flag via EC-host command for
    up to 3 seconds, in 50ms intervals. The EC will return `1` (power limited)
    so long as the charger power is < 15.001W and the battery is less than 3%.
    1.  Meanwhile, the EC performs PD negotiation. Charger power lowers to 2.5W
        for up to 500ms as the source transitions from vSafe5V to its highest
        supported voltage (15V or 20V are typical).
    1.  After negotiation is complete, the EC raises the charger current limit
        to the negotiated limit (45W is typical).
    1.  The EC returns 0 (unlimited) on the next `LIMIT_POWER` request.
1.  Depthcharge continues to boot Linux.

### High-Power Boot Device Startup

A "high-power device" in this case is one that requires significantly more than
15W of power to boot the AP. These devices may complete software sync at 15W or
less. Very briefly drawing current out of the battery does not cause a brownout.

Example configuration:

```
#define CONFIG_CHARGER_DEFAULT_CURRENT_LIMIT 512
#define CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON 3
#define CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON 15000
#define CONFIG_CHARGER_LIMIT_POWER_THRESH_CHG_MW 27000
```

Where the low-power device specified a threshold that just barely requires PD
negotiation to happen before booting, this device has a definite minimum power
to boot Linux (27W). A detailed boot sequence under this configuration, with a
low battery and available AC power:

1.  EC ROM bootloader loads and jumps to the EC's read-only firmware image.
1.  RO firmware negotiates a 15W state via Current Source analog signaling and
    begins charging the battery with it.
1.  RO firmware verifies conditions to begin booting the AP:
    -   battery state of charge >= 3%
    -   OR charger power greater or equal to 15W (met by Current Source analog
        signaling).
1.  AP firmware performs verification of the EC's RW image, upgrades it if
    necessary, and sysjumps the EC to it.
1.  AP firmware polls the charge state limit power flag via EC-host command for
    up to 3 seconds, in 50ms intervals. The EC will return `1` (power limited)
    so long as the charger power is < 27W and the battery is less than 3%.
    1.  Meanwhile, the EC performs PD negotiation. Charger power lowers to 2.5W
        for up to 500ms as the source transitions from vSafe5V to its highest
        supported voltage (15V or 20V are typical).
    1.  After negotiation is complete, the EC raises the charger current limit
        to the negotiated limit (45W is typical).
    1.  The EC returns 0 (unlimited) on the next `LIMIT_POWER` request.
1.  Depthcharge continues to boot Linux.

### High-Power SwSync Device Startup

Like the high-power boot device startup, these devices draw less than 15W during
most of the software sync process, but may briefly exceed 15W during short
intervals of software sync. However, there is substantial risk of brownout
during those intervals unless the battery is charged up a bit first. Therefore,
they strictly require 1% battery capacity to perform software sync.
Additionally, this configuration requires PD negotiation to be complete prior to
performing a no-battery boot. Nami is an exemplar.

Example configuration:

```
#define CONFIG_CHARGER_DEFAULT_CURRENT_LIMIT 512

#define CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON_WITH_AC 1
#define CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON_WITH_BATT 15000

#define CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON 3
#define CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON 27000

#define CONFIG_CHARGER_LIMIT_POWER_THRESH_BAT_PCT 3
#define CONFIG_CHARGER_LIMIT_POWER_THRESH_CHG_MW 27000
```

1.  EC ROM bootloader loads and jumps to the EC's read-only firmware image.
1.  RO firmware negotiates a 15W state via Current Source analog signaling and
    begins charging the battery with it.
1.  RO firmware verifies conditions to begin booting the AP:
    -   Battery state of charge is greater than 1% AND charger power is greater
        than 15W (met after a minute or so of charging on analog signaling)
    -   OR Battery state of charge is greater than 3%
    -   OR Charger power is greater than 27W (met after PD negotiation in
        unlocked RO firmware).
1.  AP firmware performs verification of the EC's RW image, upgrades it if
    necessary, and sysjumps the EC to it.
1.  AP firmware polls the charge state limit power flag via EC-host command for
    up to 3 seconds, in 50ms intervals. The EC will return `1` (power limited)
    so long as the charger power is < 27W and the battery is less than 3%.
    1.  Meanwhile, the EC performs PD negotiation. Charger power lowers to 2.5W
        for up to 500ms as the source transitions from vSafe5V to its highest
        supported voltage (15V or 20V are typical).
    1.  After negotiation is complete, the EC raises the charger current limit
        to the negotiated limit (45W is typical).
    1.  The EC returns 0 (unlimited) on the next `LIMIT_POWER` request.
1.  Depthcharge continues to boot Linux.

## Configuration Option Details

### `CONFIG_CHARGER_DEFAULT_CURRENT_LIMIT`

Required.

The default charger current limit used on startup and for inactive ports. It
should not be higher than 512 mA unless the device ships with a discrete power
supply. Raising this term above 512 mA is contrary to USB-PD. It may be lowered
in order to improve compatibility with marginal BC1.2 chargers.

### `CONFIG_CHARGER_MIN_INPUT_CURRENT_LIMIT`

Optional.

If set, charger input current limits will never be set lower than this value.
Historically most boards used the same value
as `CONFIG_CHARGER_DEFAULT_CURRENT_LIMIT`, but doing so violates USB-PD standby
power requirements when voltages greater than 5V are used with the default 512
mA value. Configuring this option to a nonzero value may be useful if a board
needs extra headroom (possibly at the cost of drawing excess standby power), but
boards should prefer to override `board_set_charge_limit()` instead to limit
situations with excess power draw to only occur when that extra power is needed.

### `CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON`

Required.

The minimum battery state of charge to start up the AP, in percent of full
charge.

#### `CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON`

Default: 15000 (15W)

The minimum charger power level to start the AP even when the battery is less
than `CHARGER_MIN_BAT_PCT_FOR_POWER_ON`, in milliwatts.

### `CONFIG_BATTERY_MEASURE_IMBALANCE`

Optional. Only set this option if one or more batteries shipped with this board
support per-cell battery voltage measurement.

When enabled, the EC will query the attached battery for its per-cell voltages.
If the cell voltage is excessively imbalanced at a low state of charge, the boot
is inhibited.

#### `CONFIG_CHARGER_MIN_BAT_PCT_IMBALANCED_POWER_ON`

Default: 5%. Above this battery state of charge, cell voltage balance is
ignored.

#### `CONFIG_BATTERY_MAX_IMBALANCE_MV`

Default: 200 mV. If the difference between the highest and lowest cell exceeds
this value, then the pack is considered to be imbalanced.

Note that lithium chemistry cells will almost always read similar voltages. It
is only near the top and bottom of charge that the slope of dV/dQ increases
enough for small cell imbalances to be visible as a voltage difference.

### `CONFIG_CHARGER_LIMIT_POWER_THRESH_CHG_MW`

Optional.

The minimum charger power level to allow Depthcharge to start up the kernel,
even when the battery state of charge is less than
`CHARGER_LIMIT_POWER_THRESH_BAT_PCT`, in milliwatts.

When this term is `#undef`ined (the default), kernel startup is immediately
allowed.

#### `CONFIG_CHARGER_LIMIT_POWER_THRESH_BAT_PCT`

Optional.

The minimum battery state of charge to allow Depthcharge to start up the kernel.
When using this feature, start with `CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON`

### `CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON_WITH_AC`

Optional.

Similar to `MIN_BAT_PCT_FOR_POWER_ON`, but used to define a secondary threshold
for this feature.

#### `CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON_WITH_BATT`

Optional.

Similar to `CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON`, this is the minimum
charger power needed to boot even when the battery is less than
`CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON_WITH_AC`

### `CONFIG_CHARGER_INPUT_CURRENT_DERATE_PCT`

Optional, default 5%.

This option reduces the charger's programmed current limit below the detected
current limit for a given charger in an attempt to ensure that load transients
won't overcurrent the source. Devices that require a lot of power to boot may
need to either decrease the derating factor (if behavior remains acceptable when
decreased) or increase `MIN_POWER_MW` settings to compensate.
