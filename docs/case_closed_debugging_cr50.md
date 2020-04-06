# Google Security Chip (GSC) Case Closed Debugging (CCD)

Cr50 is the firmware that runs on the Google Security Chip (GSC), which has
support for [Case Closed Debugging](CCD).

This document explains how to setup CCD, so you can access all of the necessary
features to develop firmware on your Chrome OS device, access
[debug consoles][consoles], and [disable hardware write protect][hw-wp].

[TOC]

## Overview

Cr50 CCD was designed to restrict CCD access to device owners and is implemented
through **CCD privilege levels** ([`Open`], [`Unlocked`], [`Locked`]) that can
be used to enable access to different **[CCD capabilities][cap]**. Capability
settings can be modified to require certain privilege levels to access each
capability. Device owners can use these settings to customize CCD so that it is
as open or restricted as they want.

Cr50 CCD exposes [3 debug consoles][consoles]: AP, EC, and Cr50 as well as
control over [Hardware Write Protect][hw-wp]. Cr50 CCD also allows
[flashing the AP firmware] or [flashing the EC firmware].

### Capability and Privilege Levels {#cap-priv}

Privilege Levels |
---------------- |
`Open`           |
`Unlocked`       |
`Locked`         |

Capability Setting | Definition
------------------ | ----------
`IfOpened`         | Specified capability is allowed if Cr50 Privilege Level is `Open`.
`UnlessLocked`     | Specified capability is allowed unless Cr50 Privilege Level is `Locked`.
`Always`           | Specified capability is always allowed, regardless of Cr50 Privilege Level.

Capability Setting | Privilege Level Required
------------------ | ----------------------------------------
`IfOpened`         | `Open`
`UnlessLocked`     | `Open` or `Unlocked`
`Always`           | `Open`, `Unlocked`, `Locked` (any state)

## CCD Capabilities {#cap}

The default Cr50 privilege level is [`Locked`] with the following capability
settings:

Capability        | Default    | Function
----------------- | ---------- | --------
`UartGscRxAPTx`   | `Always`   | AP console read access
`UartGscTxAPRx`   | `Always`   | AP console write access
`UartGscRxECTx`   | `Always`   | EC console read access
`UartGscTxECRx`   | `IfOpened` | EC console write access
[`FlashAP`]       | `IfOpened` | Allows flashing the AP
[`FlashEC`]       | `IfOpened` | Allows flashing the EC
[`OverrideWP`]    | `IfOpened` | Override hardware write protect
`RebootECAP`      | `IfOpened` | Allow rebooting the EC/AP from the Cr50 console
`GscFullConsole`  | `IfOpened` | Allow access to restricted Cr50 console commands
`UnlockNoReboot`  | `Always`   | Allow unlocking Cr50 without rebooting the AP
`UnlockNoShortPP` | `Always`   | Allow unlocking Cr50 without physical presence
`OpenNoTPMWipe`   | `IfOpened` | Allow opening Cr50 without wiping the TPM
`OpenNoLongPP`    | `IfOpened` | Allow opening Cr50 without physical presence
`BatteryBypassPP` | `Always`   | Allow opening Cr50 without physical presence and developer mode if the battery is removed
`UpdateNoTPMWipe` | `Always`   | Allow updating Cr50 without wiping the TPM
`I2C`             | `IfOpened` | Allow access to the I2C master (used for measuring power)
`FlashRead`       | `Always`   | Allow dumping a hash of the AP or EC flash
`OpenNoDevMode`   | `IfOpened` | Allow opening Cr50 without developer mode
`OpenFromUSB`     | `IfOpened` | Allow opening Cr50 from USB

## Consoles {#consoles}

Cr50 presents 3 consoles through CCD: AP, EC, and Cr50, each of which show up on
your host machine as a `/dev/ttyUSBX` device when a debug cable ([Suzy-Q] or
[Type-C Servo v4]) is plugged in to the DUT.

Console | Default access                              | Capability Name
------- | ------------------------------------------- | ---------------
Cr50    | always read/write, but commands are limited | `GscFullConsole` enables the full set of Cr50 console commands
AP      | read/write                                  | `UartGscRxAPTx` / `UartGscTxAPRx`
EC      | read-only                                   | `UartGscRxECTx` / `UartGscTxECRx`

### Connecting to a Console

When a debug cable ([Suzy-Q] or [Type-C Servo v4]) is plugged in to the DUT, the
3 consoles will show up as `/dev/ttyUSBX` devices. You can connect to them with
your favorite terminal program (e.g., `minicom`, `screen`, etc). You can also
use the [`usb_console`] command to connect to Cr50 (`18d1:5014`) and specify the
interface to choose between the consoles.

```bash
# Install `usb_console`
(chroot) sudo emerge ec-devutils
```

```bash
# Connect to Cr50 console
(chroot) $ usb_console -d 18d1:5014
```

```bash
# Connect to AP console
(chroot) $ usb_console -d 18d1:5014 -i 1
```

```bash
# Connect to EC console
(chroot) $ usb_console -d 18d1:5014 -i 2
```

#### Using "servod" to access the console

[`servod`] can be used to create alternative console devices when combined with
a [Servo].

First, make sure your [servo firmware is updated][update servo v4].

Next, start [`servod`]:

```bash
(chroot) $ sudo servod -b $BOARD
```

Then use `dut-control` to display the console devices:

```bash
(chroot) $ dut-control cr50_uart_pty ec_uart_pty cpu_uart_pty
```

Connect to the console devices with your favorite terminal program (e.g.,
`minicom`, `screen`, etc.).

## CCD Open {#ccd-open}

Some basic CCD functionality is accessible by default: read-only access to the
EC console, read-write access to the AP console, and a few basic Cr50 console
commands. Note that while Cr50 has read-write access to the AP console by
default, the AP console itself is disabled for production devices.

In order to access all CCD functionality or to modify capability settings, Cr50
CCD needs to be [`Open`].

1.  Connect to the Cr50 console by connecting a [Suzy-Q] or [Type-C Servo v4] to
    the DUT and running the following command:

    ```bash
    (chroot) $ usb_console -d 18d1:5014
    ```

    *** note
    **NOTE**: If another program is already connected to the Cr50 console,
    you'll see `tx [Errno 16] Resource Busy`. For example, this will happen if
    [`servod`] is running.
    ***

1.  At the Cr50 console, use the `version` command to make sure you have a
    recent enough version to use CCD. The relevant version is either `RW_A` or
    `RW_B`, whichever has the asterisk next to it:

    ```
    cr50 > version

    Chip:    g cr50 B2-C
    Board:   0
    RO_A:  * 0.0.10/29d77172
    RO_B:    0.0.10/c2a3f8f9
    RW_A:  * 0.3.23/cr50_v1.9308_87_mp.320-aa1dd98  <---- This is the version
    RW_B:    0.3.18/cr50_v1.9308_87_mp.236-8052858
    BID A:   00000000:00000000:00000000 Yes
    BID B:   00000000:00000000:00000000 Yes
    Build:   0.3.23/cr50_v1.9308_87_mp.320-aa1dd98
             tpm2:v1.9308_26_0.36-d1631ea
             cryptoc:v1.9308_26_0.2-a4a45f5
             2019-10-14 19:18:05 @chromeos-ci-legacy-us-central2
    ```

1.  Production (`MP`) versions of Cr50 firmware use a [minor version][semver] of
    `3`: `0.3.x`. Production firmware versions `0.3.9` or newer support CCD.

    Development (`PrePVT`) versions of Cr50 firmware use a minor version of
    `4`: `0.4.x`. Development firmware versions `0.4.9` or newer support CCD.

    Your device likely supports CCD if it was manufactured in the last few
    years. If you have an older version, follow the [Updating Cr50] instructions
    before continuing.

1.  Put the device into [Recovery Mode] and enable [Developer Mode].

    *** note
    **NOTE**: Developer Mode has to be enabled as described. Using GBB flags to
    force Developer Mode will not work.
    ***

    If you can't put your device into [Developer Mode] because it doesn't boot,
    follow the [CCD Open Without Booting the Device] instructions.

1.  Verify Cr50 knows the device is in [Developer Mode] by finding `TPM:
    dev_mode` in the Cr50 console `ccd` command output:

    ```
    cr50 > ccd
          ...
          TPM: dev_mode                     <==== This is the important part
          ...
    ```

1.  Start the CCD open process from the AP.

    ```bash
    (dut) $ gsctool -a -o
    ```

1.  Over the next 5 minutes you will be prompted to press the power button
    multiple times. After the last power button press the device will reboot.

    *** note
    **WARNING**: Opening CCD causes Cr50 to forget that it is in
    [Developer Mode], so when the device reboots, it will say that the OS
    image is invalid. Use the key combinations to enter [Recovery Mode] and
    re-enable [Developer Mode]. See [this bug] for details.
    ***

1.  Use the `ccd` command on the Cr50 console to verify the state is [`Open`]:

    ```
    cr50 > ccd

    State: Opened
    ...
    ```

1.  **The [`Open`] state is lost if Cr50 reboots, the device loses power (e.g.,
    battery runs out and AC is not plugged in), or the battery is removed. Note
    that Cr50 does not reboot when the system reboots; it only reboots if it is
    updated, the devices loses power, the battery runs out, or it crashes**. If
    you plan on [flashing the AP firmware] or [flashing the EC firmware], it is
    recommended you modify the capability settings or set a CCD password, so you
    can reopen the device in the case that you accidentally brick it with bad
    firmware. The simplest way to do this is to reset to factory settings and
    enable testlab mode:

    ```
    cr50 > ccd reset factory
    ```

    ```
    cr50 > ccd testlab enable
    ```

    For full details, see the section on [CCD Open Without Booting the Device].

## Configuring CCD Capability Settings

Cr50 capabilities allow you to configure CCD to restrict or open the device as
much as you want. You can use the `ccd` command on the Cr50 console to check and
modify the capabilities, but CCD has to be [`Open`] to change the capabilities.

Setting capabilities you want to use to [`Always`] will make them accessible
even if CCD loses the [`Open`] state, which happens when Cr50 reboots or the
device loses power.

Basic CCD functionality is covered by `UartGscTxECRx`, `UartGscRxECTx`,
`UartGscTxAPRx`, `UartGscRxAPTx`, [`FlashAP`], [`FlashEC`], [`OverrideWP`], and
`GscFullConsole`.

```
cr50 > ccd set $CAPABILITY $REQUIREMENT
```

### Examples

#### EC Console

If the EC console needs to be read-write even when CCD is [`Locked`] set the
capability to [`Always`]:

```
cr50 > ccd set UartGscTxECRx Always
```

#### Restrict Consoles

If you want to restrict capabilities more than [`Always`], you can set them to
[`IfOpened`], which will make it so that it is only accessible when CCD is
[`Open`]ed, not [`Lock`]ed:

##### Restrict EC

```
cr50 > ccd set UartGscTxECRx IfOpened
cr50 > ccd set UartGscRxECTx IfOpened
```

##### Restrict AP

```
cr50 > ccd set UartGscTxAPRx IfOpened
cr50 > ccd set UartGscRxAPTx IfOpened
```

#### Most Accessible

If you want things as accessible as possible and want all capabilities to be
[`Always`], you can run

```
cr50 > ccd reset factory
```

This will also permanently disable write protect. To reset write protect run

```
cr50 > wp follow_batt_pres atboot
```

To reset capabilities to Default run

```
cr50 > ccd reset
```

## Flashing EC {#flashec}

Flashing the EC is restricted by the `FlashEC` capability.

The steps to flash the EC differ based on the board being used, but the
[`flash_ec`] script will handle this for you.

```bash
(chroot) $ sudo servod -b $BOARD
(chroot) $ ~/trunk/src/platform/ec/util/flash_ec -i $IMAGE -b $BOARD
```

## Flashing the AP {#flashap}

*** note
**WARNING**: Before attempting to flash the AP firmware, start with the
[CCD Open] steps; if you flash broken firmware before opening CCD, you may make
it impossible to restore your device to a working state.
***

Flashing the AP is restricted by the `FlashAP` capability.

```bash
(chroot) $ sudo flashrom -p raiden_debug_spi:target=AP -w $IMAGE
```

This default flashing command takes a very long time to complete, there are ways
to [speed up the flashing process] by cutting some corners.

If you have many CCD devices connected, you may want to use the Cr50 serial
number:

```bash
(chroot) $ lsusb -vd 18d1:5014 | grep iSerial
```

You can then add the serial number to the [`flashrom`] command:

```bash
(chroot) $ sudo flashrom -p raiden_debug_spi:target=AP,serial=$SERIAL -w $IMAGE
```

**If you don't see Cr50 print any messages when you're running the [`flashrom`]
command and you have more than one Cr50 device connected to your workstation,
you probably need to use the serial number.**

### Special Cases {#flashap-special-cases}

Cr50 puts the device in reset to flash the AP. Due to hardware limitations Cr50
may not be able to disable hardware write protect while the device is in reset.
If you want to reflash the AP RO firmware using CCD and your board has issues
disabling hardware write protect, you may need to also disable software write
protect.

To determine if the board you are using has this issue:

1.  Disable write protect using the Cr50 console command:

    ```
    cr50 > wp disable
    ```

1.  Check if hardware write protect disabled when the AP is off:

    ```bash
    (chroot) $ sudo flashrom -p raiden_debug_spi:target=AP --wp-status
    ```

1.  If the last command shows that hardware write protect is still enabled when
    the AP is off, then you need to disable software write protect:

    ```bash
    (chroot) $ flashrom -p host --wp-disable
    ```

## Control Hardware Write Protect {#hw-wp}

Control of hardware write protect is restricted by the `OverrideWP` capability.
When the capability is allowed, the hardware write protect setting can be
controlled with the `wp` command in the Cr50 console. Otherwise, the hardware
write protect is determined based on the presence of the battery.

Hardware Write Protect Setting | Battery State                  | Hardware Write Protect State
------------------------------ | ------------------------------ | ----------------------------
`follow_batt_pres`             | Connected                      | Enabled
`follow_batt_pres`             | Disconnected                   | Disabled
`follow_batt_pres`             | N/A (Chromebox has no battery) | Write Protect Screw means Enabled
`enabled`                      | Any                            | Enabled
`disabled`                     | Any                            | Disabled

### Write Protect Commands

```
cr50 > wp [enable|disable|follow_batt_pres]
```

There are two write protect settings: the current setting and the `atboot`
setting.

The `wp` command adjusts the current write protect setting that will last until
Cr50 reboots or loses power. Note that Cr50 does not reboot when the rest of the
system reboots. It will only reboot in the cases where the firmware is being
updated, it crashes, the battery completely drains, the battery is removed, or
power is otherwise lost.

The `atboot` setting is the state of the write protect when Cr50 boots; it
defaults to `follow_batt_pres`.

To change the `atboot` setting, add the `atboot` arg to the `wp` command:

```
cr50 > wp atboot [enable|disable|follow_batt_pres]
```

You can query the write protect state with `gsctool`:

```bash
(dut) $ gsctool -a -w

...
Flash WP: forced disabled  <-- Current hardware write protect state
 at boot: forced disabled  <-- "atboot" hardware write protect state

```

`gsctool -a -w` Status | Hardware Write Protect State
---------------------- | ------------------------------------
`forced disabled`      | Disabled
`forced enabled`       | Enabled
`enabled`              | Enabled, following battery presence
`disabled`             | Disabled, following battery presence

### Special Case Devices

Bob devices have a write protect screw in addition to battery presence. The
write protect screw will force enable write protect until it's removed. If Cr50
is set to `follow_batt_pres`, you need to remove the write protect screw and
disconnect the battery to disable write protect. If you run `wp disable`, you
will also need to remove the screw.

If you are attempting to flash the AP, see the [Flashing the AP Special Cases]
section for additional steps you may have to take to disable write protection.

## UART Rescue mode

### Overview

UART Rescue Mode is a feature of the Cr50 RO firmware that supports programming
the RW firmware using only the UART interface. This is used to recover a bad RW
firmware update (which should be rare).

This is also useful when bringing up new designs, as this allows to update Cr50
image even before USB CCD or TPM interfaces are operational.

UART rescue works on all existing devices, all it requires is that Cr50 console
is mapped to a `/dev/xxx` device on the workstation (the same device used to
attach a terminal to the console).

Rescue works as follows: when the RO starts, after printing the regular banner
on the console it prints a magic string to the console and momentarily waits for
the host to send a sync symbol, to indicate that an alternative RW will have to
be loaded over UART. The RO also enters this mode if there is no valid RW to
run.

When rescue mode is triggered, the RO is expecting the host to transfer a single
RW image in hex format.

### Install the cr50-rescue utility

The `cr50-rescue` utility is used to flash a given firmware to Cr50 using rescue
mode. This tool must be installed inside the chroot.

```bash
(chroot) $ sudo emerge cr50-utils
```

### Preparing an RW image

To prepare the signed hex RW image, fetch a released image from Google storage,
which can be found by running:

```bash
(chroot) $ gsutil ls gs://chromeos-localmirror/distfiles/cr50*
```

(depending on your setup you might have to do this inside chroot). Copy the
image you want to use for rescue to your workstation and extract cr50.bin.prod
from the tarball.

The latest Cr50 images can be found in the [chromeos-cr50 ebuild]. Generally,
you should always use the PROD_IMAGE indicated in that file. Once rescued, the
user can update to the PREPVT image later if needed.

Once the binary image is ready, use the following commands to carve out the RW A
section out of it and convert it into hex format:

```bash
(chroot) $ dd if=<cr50 bin file> of=cr50.rw.bin skip=16384 count=233472 bs=1
objcopy -I binary -O ihex --change-addresses 0x44000 cr50.rw.bin cr50.rw.hex
```

then you can use `cr50.rw.hex` as the image passed to `cr50-rescue`.

### Programming the RW image with rescue mode

With servo_micro (or servo_v2 reworked for connecting to Cr50 console), run
[`servod`] and disable Cr50 ec3po and UART timestamp:

```bash
(chroot) $ dut-control cr50_uart_timestamp:off dut-control cr50_ec3po_interp_connect:off
```

Get a raw Cr50 UART device path and use it for `cr50-rescue` argument `-d`
below.

```bash
(chroot) $ dut-control raw_cr50_uart_pty
```

Prior to running `cr50-rescue`, the terminal from the Cr50 console UART must be
disconnected, and Cr50 must be unpowered-- the system needs to have AC power and
battery disconnected.

After ensuring those steps, the rescue command may be run as follows:

```bash
(chroot) $ cr50-rescue -v -i <path to the signed hex RW image> -d <cr50 console UART tty>
```

After starting the command, provide power to the board and rescue mode will
start automatically. After flashing successfully (see sample output below), Cr50
must be unpowered again, by disconnecting AC power and battery.

Note that `<cr50 console UART tty>` above has to be a direct FTDI interface,
`pty` devices created by [`servod`] do not work for this purpose. Use either
servo-micro or a USB/UART cable. Note that multifunctional *SPI-UART/FTDI/USB
cables might not work*, as they impose a significant delay in the UART stream,
which makes the synchronization described below impossible.

`cr50-rescue` starts listening on the console UART and printing it out to the
terminal. When the target is reset, `cr50-rescue` detects the `Bldr |` string in
the target output, at this point the utility intercepts the boot process and the
target proceeds to receiving the new RW image and saving it into flash. Note the
currently present RW and RW_B images will be wiped out first.

#### Sample output

```bash
(chroot) $ cr50-rescue -v -i cr50.3.24.rw.hex -d /dev/pts/0

low 00044000, high 0007cfff
base 00044000, size 00039000
..startAdr 00000000
..maxAdr 0x0003d000
..dropped to 0x0003a188
..skipping from 0x00000000 to 0x00004000
226 frames
(waiting for "Bldr |")
Havn2|00000000_000000@0
exp  ?36
Himg =2CD687F2B1579ED1E85C7F35055550A63B9B146E2CAC808295C59F97849F08E7
Hfss =184D83B3D89599C90E4852EF16F9FAEEEED07BC0AFDF1028136AA3C9F71D4F43
Hinf =44D21600B3723BDB0DCB9E0891E9F7373FC1BDE69598C9D7F04B1ABEB70529BD
exp  ?40
exp  ?48
exp  ?67
jump @00080400

Bldr |(waiting for "oops?|")1527394
retry|0
oops?|0.1.2.3.4.5.6.7.8.9.10.11.12.13.14.15.16.17.18.19.20.21.22.23.24.25.26.27.28.29.30.31.32.33.34.35.36.37.38.39.40.41.42.43.44.45.46.47.48.49.50.51.52.53.54.55.56.57.58.59.60.61.62.63.64.65.66.67.68.69.70.71.72.73.74.75.76.77.78.79.80.81.82.83.84.85.86.87.88.89.90.91.92.93.94.95.96.97.98.99.100.101.102.103.104.105.106.107.108.109.110.111.112.113.114.115.116.117.118.119.120.121.122.123.124.125.126.127.128.129.130.131.132.133.134.135.136.137.138.139.140.141.142.143.144.145.146.147.148.149.150.151.152.153.154.155.156.157.158.159.160.161.162.163.164.165.166.167.168.169.170.171.172.173.174.175.176.177.178.179.180.181.182.183.184.185.186.187.188.189.190.191.192.193.194.195.196.197.198.199.200.201.202.203.204.205.206.207.208.209.210.211.212.213.214.215.216.217.218.219.220.221.222.223.224.225.done!
```

## CCD Open Without Booting the Device {#ccd-open-no-boot}

If you can’t boot your device, you won’t be able to enable [Developer Mode] to
send the open command from the AP. If you have enabled CCD on the device before,
Cr50 may be configured in a way that you can still open Cr50.

### Option 1: Remove the battery

If you can remove the battery, you can bypass the [Developer Mode] requirements.
`ccd open` is allowed from the Cr50 console if the Chrome OS Firmware Management
Parameters (`FWMP`) do not disable CCD and the battery is disconnected. This is
the most universal method and will work even if you haven’t enabled CCD before.

1.  Disconnect the battery

1.  Send `ccd open` from the Cr50 console.

### Option 2: "OpenNoDevMode" and "OpenFromUSB" are set to Always

If "OpenNoDevMode" and "OpenFromUSB" are set to Always, you will be able to open
Cr50 from the Cr50 console without enabling [Developer Mode]:

```
cr50 > ccd open
```

You will still need physical presence (i.e., press the power button) unless
`testlab` mode is also enabled:

```
cr50 > ccd testlab
       CCD test lab mode enabled
```

#### Enabling

If CCD is [`Open`], you can enable these settings with:

```
cr50 > ccd set OpenFromUSB Always
cr50 > ccd set OpenNoDevMode Always
```

### Option 3: CCD Password is Set

If the CCD password is set, you can open from the Cr50 console without
[Developer Mode].

```
cr50 > ccd open $PASSWORD
cr50 > ccd unlock $PASSWORD
```

Alternatively, you can use `gsctool`, entering the password when prompted:

```
(dut) $ gsctool -a -o
(dut) $ gsctool -a -u
```

#### Enabling

When CCD is [`Open`], run the `gsctool` command and enter the password when
prompted.

```bash
(chroot) $ gsctool -a -P
```

You can use the CCD command on the Cr50 console to check if the password is set.

```
cr50 > ccd
       ...
       Password: [none|set]
       ...
```

#### Disabling

When CCD is [`Open`], you can use `gsctool` to clear the password:

```bash
(dut) $ gsctool -a -P clear:$PASSWORD
```

Alternatively, you can use the Cr50 console to clear the password and reset CCD
capabilities to their default values:

```
cr50 > ccd reset
```

## Troubleshooting

### rddkeepalive

Cr50 only enables CCD when it detects a debug accessory is connected (e.g.,
[Suzy-Q] or [Type-C Servo v4]). It detects the cable based on the voltages on
the CC lines. If you are flashing the EC and AP or working with unstable
hardware, these CC voltages may become unreliable for detecting a debug
accessory.

To work around this, you can force Cr50 to always assume that a debug cable is
detected:

```
cr50 > rddkeepalive enable
```

*** note
**NOTE**: Enabling `rddkeepalive` does increase power consumption.
***

To disable:

```
cr50 > rddkeepalive disable
```

### Updating Cr50 {#updating-cr50}

Production (`MP`) versions of Cr50 firmware use a [minor version][semver] of
`3`: `0.3.x`. Production firmware versions `0.3.9` or newer support CCD.

Development (`PrePVT`) versions of Cr50 firmware use a minor version of `4`:
`0.4.x`. Development firmware versions `0.4.9` or newer support CCD.

There aren't many differences between the MP and PrePVT versions of images, but
it is a little easier to CCD [`Open`] PrePVT images. You can't run PrePVT images
on MP devices, so if you're trying to update to PrePVT and it fails try using
the MP image.

1.  Flash a test image newer than M66.

1.  Enable [Developer Mode] and connect a debug cable ([`Suzy-Q`] or [`Type-C
    Servo v4`]).

1.  Check the running Cr50 version with `gsctool`:

```bash
(dut) $ sudo gsctool -a -f

...
RW 0.4.26  <-- The "RW" version is the one to check
```

1.  Update Cr50 using the firmware in the OS image:

*Production (MP) image*:

```bash
(dut) $ sudo gsctool -a /opt/google/cr50/firmware/cr50.bin.prod
```

*Development (PrePVT) image*:

```bash
(dut) $ sudo gsctool -a /opt/google/cr50/firmware/cr50.bin.prepvt
```

1.  Check the Cr50 version again to make sure it's either `0.3.X` or `0.4.X`.

### Speed up Flashing the AP {#speed-up-ap-flash}

In the [default AP flashing steps][flashap] [`flashrom`] reads the entire flash
contents and only erases and programs the pages that have to be modified.
However, when Cr50 controls the SPI bus, it can only run at 1.5 MHz, versus the
50 MHz that the AP normally runs it at.

We can take advantage of the fact that Chrome OS device AP firmware is split
into sections, only a few of which are essential for maintaining the device
identity and for booting the device in recovery mode to program faster by only
reading and writing sections we care about:

```bash
# This will save device flash map and VPD sections in
# /tmp/bios.essentials.bin. VPD sections contain information like device
# firmware ID, WiFi calibration, enrollment status, etc. Use the below command
# only if you need to preserve the DUT's identity, no need to run it in case
# the DUT flash is not programmed at all, or you do not care about preserving
# the device identity.
sudo flashrom -p raiden_debug_spi:target=AP -i FMAP -i RO_VPD -i RW_VPD -r /tmp/bios.essentials.bin --fast-verify

# This command will erase the entire flash chip in one shot, the fastest
# possible way to erase.
sudo flashrom -p raiden_debug_spi:target=AP -E --do-not-diff

# This command will program essential flash sections necessary for the
# Chrome OS device to boot in recovery mode. Note that the SI_ALL section is
# not always present in the flash image, do not include it if it is not in
# dump_fmap output.
sudo flashrom -p raiden_debug_spi:target=AP -w image-atlas.bin -i FMAP -i WP_RO [-i SI_ALL] --do-not-diff --noverify

# This command will restore the previously preserved VPD sections of the
# flash, provided it was saved in the first step above.
sudo flashrom -p raiden_debug_spi:target=AP -w /tmp/bios.essential.bin -i RO_VPD -i RW_VPD --do-not-diff --noverify
```

Once flash is programmed, the device can be booted in recovery mode and start
Chrome OS from external storage, following the usual recovery procedure. Once
Chrome OS is installed, AP flash can be updated to include the rest of the image
by running [`flashrom`] or `futility` from the device bash prompt.

[Case Closed Debugging]: ./case_closed_debugging.md
[chromeos-cr50 ebuild]: https://chromium.googlesource.com/chromiumos/overlays/chromiumos-overlay/+/refs/heads/master/chromeos-base/chromeos-cr50/chromeos-cr50-0.0.1.ebuild
[Developer Mode]: https://chromium.googlesource.com/chromiumos/docs/+/master/developer_mode.md#dev-mode
[Recovery Mode]: https://chromium.googlesource.com/chromiumos/docs/+/master/debug_buttons.md
[Servo]: https://chromium.googlesource.com/chromiumos/third_party/hdctools/+/master/docs/servo.md
[`servod`]: https://chromium.googlesource.com/chromiumos/third_party/hdctools/+/master/docs/servo.md
[Type-C Servo v4]: https://chromium.googlesource.com/chromiumos/third_party/hdctools/+/master/docs/servo_v4.md
[update servo v4]: https://chromium.googlesource.com/chromiumos/third_party/hdctools/+/master/docs/servo_v4.md#updating-firmware
[Suzy-Q]: https://chromium.googlesource.com/chromiumos/third_party/hdctools/+/master/docs/ccd.md#SuzyQ-SuzyQable
[`hdctools`]: https://chromium.googlesource.com/chromiumos/third_party/hdctools/+/refs/heads/master/README.md
[`FlashAP`]: #flashap
[flashing the AP firmware]: #flashap
[flashap]: #flashap
[Flashing the AP Special Cases]: #flashap-special-cases
[`FlashEC`]: #flashec
[flashing the EC firmware]: #flashec
[`OverrideWP`]: #hw-wp
[`Always`]: #cap-priv
[`IfOpened`]: #cap-priv
[`Open`]: #cap-priv
[`Locked`]: #cap-priv
[`Unlocked`]: #cap-priv
[Updating Cr50]: #updating-cr50
[CCD Open Without Booting the Device]: #ccd-open-no-boot
[cap]: #cap
[consoles]: #consoles
[hw-wp]: #hw-wp
[`flash_ec`]: https://chromium.googlesource.com/chromiumos/platform/ec/+/master/util/flash_ec
[CCD Open]: #ccd-open
[`flashrom`]: https://chromium.googlesource.com/chromiumos/third_party/flashrom/+/master/README.chromiumos
[speed up the flashing process]: #speed-up-ap-flash
[this bug]: https://issuetracker.google.com/149420712
[semver]: https://semver.org/
[`usb_console`]: https://chromium.googlesource.com/chromiumos/platform/ec/+/master/extra/usb_serial/console.py
