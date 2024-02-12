# FPMCU Development for Partners

This document is intended to help partners (sensor vendors, MCU vendors, etc)
that are currently (or interested in) developing fingerprint solutions for
Chromebooks. The document assumes that you're using Linux to do the development;
preferably a recent version of Ubuntu or Debian. Some partners have had success
developing in a VM, but please note that we don't test that configuration.

See the [FPMCU documentation] for additional development information.

[TOC]

## Hardware Required for Standalone Development (no Chromebook)

The following hardware components can be used to set up a standalone development
environment for FPMCU development (i.e., it does not rely on a Chromebook).
Development for other [EC]s is often done in a similar manner, but some of them
have their own standalone development or evaluation kits that don't require the
use of [servo].

You will need an [FPMCU reference board](#fpmcu-dev-board) and a
[servo debugger](#servo).

### FPMCU board {#fpmcu-dev-board}

The Fingerprint MCU (FPMCU) board has the MCU that handles all
fingerprint-related functionality (matching, encryption, etc). The fingerprint
sensor itself connects to the FPMCU board.

<!-- mdformat off(b/139308852) -->
*** note
Older revisions of these boards are not supported. If you have an older version
of the board, please request a replacement.
***
<!-- mdformat on -->

This FPMCU board is the Dragonclaw Rev 0.3. |
------------------------------------------- |
![Dragonclaw board]                         |

Download the [Dragonclaw schematics, layout, and BOM][dragonclaw schematics].

<!-- mdformat off(b/139308852) -->
*** note
**Googlers**: You can pick up a Dragonclaw development board at Chromestop.

**Partners**: You can request a Dragonclaw development board from Google.
***
<!-- mdformat on -->

This FPMCU board is Icetower Rev 3. |
----------------------------------- |
![Icetower board]                   |

<!-- mdformat off(b/139308852) -->
*** note
**Googlers**: You can pick up an Icetower development board at Chromestop.

**Partners**: You can request an Icetower development board from Google.
***
<!-- mdformat on -->

This FPMCU board is Quincy Rev 2. |
--------------------------------- |
![Quincy board]                   |

<!-- mdformat off(b/139308852) -->
*** note
**Googlers**: You can pick up an Quincy development board at Chromestop.

**Partners**: You can request an Quincy development board from Google.
***
<!-- mdformat on -->

### Servo

Servo is a general purpose debug board that connects to a header on the FPMCU
board. Among other things, the servo supplies power to the FPMCU and can be used
to program the FPMCU, interact with the EC console, take power measurements, and
debug a running program.

There are several variants of Servo and the fingerprint team uses the
[Servo Micro](#servo-micro) for its simplicity. It lacks builtin JTAG/SWD
support for single step debugging, but Dragonclaw v0.3 and Icetower v3 have an
[SWD connector](#servo-micro-swd) that can be used.

[Servo Micro](#servo-micro) |
--------------------------- |
![Servo Micro]              |

<!-- mdformat off(b/139308852) -->
*** note
For more information about Servo, see [servo].
***
<!-- mdformat on -->

### Servo Micro

Servo Micro does not require any adapters to interface with the FPMCU board.

As you can see below, one end connects to the FPMCU board and the other connect
to the developer's computer over micro USB.

![Servo Micro with Dragonclaw]

<!-- mdformat off(b/139308852) -->
*** note
For more information about Servo Micro, see [Servo Micro Info].
***
<!-- mdformat on -->

#### Using SWD (Optional) {#servo-micro-swd}

Instructions for setup are described in [Fingerprint Debugging].

## Software Setup

### Get the Chromium OS source code

*   First, make sure you [have the prerequisites].
*   Then [get the source].
*   Create and [enter the `chroot`].
    *   You can stop after the `enter the chroot` step.

### Build the [EC]\ (embedded controller) codebase

Open **two** terminals and enter the chroot in each:

```bash
# from a terminal on your machine
(outside chroot) $ cd ~/chromiumos/src

# enter the chroot
(outside chroot) $ cros_sdk
```

<!-- mdformat off(b/139308852) -->
*** note
NOTE: More information on servo can be found in the [servo] documentation.
***
<!-- mdformat on -->

In one of the terminals, build and start `servod`

Build and install `servod` in the chroot:

```bash
(chroot) $ sudo emerge hdctools
```

<!-- mdformat off(b/139308852) -->
*** note
In all of the following commands, replace `<BOARD>` in the command with
`bloonchipper` or `dartmonkey` depending on the development board you are using.
***
<!-- mdformat on -->

Run `servod`:

```bash
(chroot) $ sudo servod --board=<BOARD>
```

You should see something like this. Leave it running:

```bash
2023-02-03 15:05:30,632 - servod - INFO - Start
2023-02-03 15:05:30,639 - servod - INFO - Found XML overlay for board icetower
2023-02-03 15:05:30,639 - SystemConfig - INFO - Loading XML config (/usr/lib64/python3.6/site-packages/servo/data/servo_micro.xml, None, 0)
2023-02-03 15:05:30,640 - SystemConfig - INFO - Loading XML config (/usr/lib64/python3.6/site-packages/servo/data/common.xml, None, 0)
2023-02-03 15:05:30,640 - SystemConfig - INFO - Loading XML config (/usr/lib64/python3.6/site-packages/servo/data/power_tools.xml, None, 0)
2023-02-03 15:05:30,642 - SystemConfig - INFO - Loading XML config (/usr/lib64/python3.6/site-packages/servo/data/keyboard.xml, None, 0)
2023-02-03 15:05:30,644 - SystemConfig - INFO - Loading XML config (/usr/lib64/python3.6/site-packages/servo/data/uart_common.xml, None, 0)
2023-02-03 15:05:30,644 - SystemConfig - INFO - Loading XML config (/usr/lib64/python3.6/site-packages/servo/data/ec_info.xml, None, 0)
2023-02-03 15:05:30,645 - SystemConfig - INFO - Loading XML config (/usr/lib64/python3.6/site-packages/servo/data/cpu_fw_spi.xml, None, 0)
2023-02-03 15:05:30,645 - SystemConfig - INFO - Loading XML config (/usr/lib64/python3.6/site-packages/servo/data/servo_adc_enabled_noop.xml, None, 0)
2023-02-03 15:05:30,648 - SystemConfig - INFO - Loading XML config (/usr/lib64/python3.6/site-packages/servo/data/servo_icetower_overlay.xml, None, 0)
2023-02-03 15:05:30,648 - SystemConfig - INFO - Loading XML config (/usr/lib64/python3.6/site-packages/servo/data/servo_fpmcu_dev_board_common_overlay.xml, None, 0)
2023-02-03 15:05:30,649 - SystemConfig - INFO - Loading XML config (/usr/lib64/python3.6/site-packages/servo/data/servoflex_v2_r0_p50.xml, None, 0)
2023-02-03 15:05:30,649 - SystemConfig - INFO - Loading XML config (/usr/lib64/python3.6/site-packages/servo/data/fpmcu_dev_board_common_inas.xml, None, 0)
2023-02-03 15:05:30,649 - SystemConfig - INFO - Loading XML config (/usr/lib64/python3.6/site-packages/servo/data/ina2xx.xml, None, 0)
2023-02-03 15:05:30,650 - SystemConfig - INFO - Loading XML config (/usr/lib64/python3.6/site-packages/servo/data/servo_fpmcu_dev_board_uart_common_overlay.xml, None, 0)
2023-02-03 15:05:30,655 - Servod - INFO - Initializing interface 1 to stm32_uart
2023-02-03 15:05:30,655 - Interface.Build - INFO - Suart: interface: {'name': 'stm32_uart', 'interface': 0, 'index': 1}
2023-02-03 15:05:30,661 - Interface.Build - INFO - /dev/pts/2
2023-02-03 15:05:30,661 - Servod - INFO - Initializing interface 2 to stm32_i2c
2023-02-03 15:05:30,661 - Interface.Build - INFO - Si2cBus: interface: {'name': 'stm32_i2c', 'interface': 4, 'index': 2}
2023-02-03 15:05:30,661 - root - INFO - Skipping modprobe of i2c-dev: it is already loaded per existence of: /sys/module/i2c_dev/
2023-02-03 15:05:30,661 - root - INFO - Executing command: ['modprobe', '--quiet', '--', 'i2c-pseudo']
2023-02-03 15:05:30,664 - i2c_base - INFO - path b'/dev/i2c-pseudo-controller' not found, cannot start I2C pseudo adapter
2023-02-03 15:05:30,665 - Servod - INFO - Initializing interface 3 to stm32_uart
2023-02-03 15:05:30,666 - Interface.Build - INFO - Suart: interface: {'name': 'stm32_uart', 'interface': 3, 'index': 3}
2023-02-03 15:05:30,670 - Interface.Build - INFO - /dev/pts/3
2023-02-03 15:05:30,670 - Servod - INFO - Initializing interface 6 to ec3po_uart
2023-02-03 15:05:30,673 - servo_micro - EC3PO Interface - INFO - -------------------- servo_micro console on: /dev/pts/4
2023-02-03 15:05:30,673 - Servod - INFO - Initializing interface 7 to stm32_uart
2023-02-03 15:05:30,673 - Interface.Build - INFO - Suart: interface: {'name': 'stm32_uart', 'interface': 6, 'index': 7}
2023-02-03 15:05:30,678 - Interface.Build - INFO - /dev/pts/6
2023-02-03 15:05:30,678 - Servod - INFO - Initializing interface 8 to stm32_uart
2023-02-03 15:05:30,679 - Interface.Build - INFO - Suart: interface: {'name': 'stm32_uart', 'interface': 5, 'index': 8}
2023-02-03 15:05:30,684 - Interface.Build - INFO - /dev/pts/7
2023-02-03 15:05:30,684 - Servod - INFO - Initializing interface 9 to ec3po_uart
2023-02-03 15:05:30,685 - PD/Cr50 - EC3PO Interface - INFO - -------------------- PD/Cr50 console on: /dev/pts/8
2023-02-03 15:05:30,685 - Servod - INFO - Initializing interface 10 to ec3po_uart
2023-02-03 15:05:30,687 - EC - EC3PO Interface - INFO - -------------------- EC console on: /dev/pts/10
2023-02-03 15:05:30,687 - Servod - INFO - Initializing interface 11 to ec3po_uart
2023-02-03 15:05:30,688 - CPU - EC3PO Interface - INFO - -------------------- CPU console on: /dev/pts/12
2023-02-03 15:05:31,193 - Servod - INFO - Initialized init_keyboard to on
2023-02-03 15:05:31,194 - root - WARNING - cpu_ec3po_interp_connect: '1' not found in the param values
2023-02-03 15:05:31,195 - root - WARNING - cpu_ec3po_interp_connect: update drv to get and set values from the param map {'off': '0'}
2023-02-03 15:05:31,195 - Servod - INFO - Initialized cpu_ec3po_interp_connect to off
2023-02-03 15:05:31,200 - Servod - INFO - Initialized cpu_uart_raw_debug to off
2023-02-03 15:05:31,202 - Servod - INFO - Initialized cpu_uart_timestamp to off
2023-02-03 15:05:31,204 - Servod - INFO - Initialized ec_ec3po_interp_connect to on
2023-02-03 15:05:31,205 - Servod - INFO - Initialized ec_uart_raw_debug to off
2023-02-03 15:05:31,206 - Servod - INFO - Initialized ec_uart_timestamp to on
2023-02-03 15:05:31,207 - Servod - INFO - Initialized ec_uart_flush to on
2023-02-03 15:05:31,290 - CPU - EC3PO.Console - INFO - /dev/pts/12 - Disabling uart timestamps.
2023-02-03 15:05:31,308 - Servod - INFO - Initialized uart1_en to on
2023-02-03 15:05:31,335 - Servod - INFO - Initialized servo_micro_ec_boot_mode_uut to off
2023-02-03 15:05:31,462 - Servod - INFO - Initialized servo_jtag_trst to off
2023-02-03 15:05:31,564 - Servod - INFO - Initialized spi1_buf_en to off
2023-02-03 15:05:31,664 - Servod - INFO - Initialized spi2_buf_en to off
2023-02-03 15:05:31,767 - Servod - INFO - Initialized uart2_en to on
2023-02-03 15:05:32,020 - Servod - INFO - Initialized spi1_vref to pp3300
2023-02-03 15:05:32,268 - Servod - INFO - Initialized spi2_vref to pp1800
2023-02-03 15:05:32,396 - Servod - INFO - Initialized servo_jtag_trst_dir to off
2023-02-03 15:05:32,521 - Servod - INFO - Initialized servo_jtag_tdi_dir to off
2023-02-03 15:05:32,620 - Servod - INFO - Initialized gpio_expander_reset to off
2023-02-03 15:05:32,721 - Servod - INFO - Initialized uart3_on_spi1 to off
2023-02-03 15:05:32,824 - Servod - INFO - Initialized SERVO_JTAG_TMS_DIR to off
2023-02-03 15:05:32,952 - Servod - INFO - Initialized SERVO_JTAG_TDO_SEL to off
2023-02-03 15:05:33,078 - Servod - INFO - Initialized SERVO_JTAG_TMS to off
2023-02-03 15:05:33,206 - Servod - INFO - Initialized UART3_TX_SERVO_JTAG_TCK to off
2023-02-03 15:05:33,334 - Servod - INFO - Initialized UART3_RX_JTAG_BUFFER_TO_SERVO_TDO to off
2023-02-03 15:05:33,459 - Servod - INFO - Initialized SERVO_JTAG_TDO_BUFFER_EN to off
2023-02-03 15:05:33,585 - Servod - INFO - Initialized SERVO_JTAG_TDI to off
2023-02-03 15:05:33,711 - Servod - INFO - Initialized SERVO_JTAG_RTCK to off
2023-02-03 15:05:33,808 - Servod - INFO - Initialized JTAG_BUFOUT_EN_L to off
2023-02-03 15:05:33,936 - Servod - INFO - Initialized JTAG_BUFIN_EN_L to off
2023-02-03 15:05:33,936 - Servod - INFO - Initialized usbpd_uart_routing to off
2023-02-03 15:05:33,940 - Servod - INFO - Initialized fw_wp_en to off
2023-02-03 15:05:33,943 - Servod - INFO - Initialized tp1 to off
2023-02-03 15:05:33,948 - Servod - INFO - Initialized cold_reset to off
2023-02-03 15:05:33,952 - Servod - INFO - Initialized pwr_button to release
2023-02-03 15:05:33,958 - Servod - INFO - Initialized warm_reset to on
2023-02-03 15:05:33,962 - Servod - INFO - Initialized rec_mode to on
2023-02-03 15:05:33,968 - Servod - INFO - Initialized fw_up to off
2023-02-03 15:05:33,972 - Servod - INFO - Initialized lid_open to yes
2023-02-03 15:05:33,976 - Servod - INFO - Initialized pch_disable to off
2023-02-03 15:05:33,978 - Servod - INFO - Initialized tp2 to off
2023-02-03 15:05:34,380 - servoFirmwareChecker - INFO - servo_micro firmware up to date.
2023-02-03 15:05:34,381 - Servod - INFO - Initialized servo_micro_firmware_uptodate to check
2023-02-03 15:05:34,382 - Servod - INFO - Initialized servo_ec3po_interp_connect to on
2023-02-03 15:05:34,384 - Servod - INFO - Initialized servo_micro_uart_raw_debug to off
2023-02-03 15:05:34,385 - Servod - INFO - Initialized servo_micro_uart_timestamp to on
2023-02-03 15:05:34,387 - Servod - INFO - Initialized usbpd_ec3po_interp_connect to on
2023-02-03 15:05:34,389 - Servod - INFO - Initialized jtag_vref_sel0 to pp3300
2023-02-03 15:05:34,391 - Servod - INFO - Initialized jtag_vref_sel1 to pp3300
2023-02-03 15:05:34,398 - Servod - INFO - Initialized hpd to off
2023-02-03 15:05:34,399 - Servod - INFO - Initialized fpmcu_ec3po_interp_connect to off
2023-02-03 15:05:34,401 - Servod - INFO - Initialized fpmcu_ec3po_interp_connect_1 to off
2023-02-03 15:05:34,401 - DeviceWatchdog - INFO - Watchdog setup for devices: [18d1:501a MICRO-S-2201160335]
2023-02-03 15:05:34,402 - servod - INFO - Listening on localhost port 9999
```

In the other terminal, build and flash the firmware:

Make sure the toolchain is installed:

```bash
(chroot) $ sudo cros_setup_toolchains
```

Navigate to the EC source:

```bash
(chroot) $ cd ../platform/ec
```

Build the firmware:

```bash
(chroot) $ make BOARD=<BOARD> -j
```

The resulting file will be in `build/<BOARD>/ec.bin`

Flash the firmware file:

```bash
(chroot) $ ./util/flash_ec --board=<BOARD> --image=./build/<BOARD>/ec.bin
```

Prepare a serial terminal in your chroot:

```bash
(chroot) $ sudo emerge screen
```

Connect to the UART pty:

```bash
(chroot) $ sudo screen $(dut-control raw_fpmcu_console_uart_pty | cut -d: -f2)
```

Press enter key several times (may need to wait up to 20 seconds). Then you will
see a prompt:

```
>
```

At this point you are connected to the MCU's serial (UART) console. You can list
all of the available console commands with "help":

```
> help
```

```bash
Known commands:
  chan           fpcapture      hcdebugsherase     fpenroll       history        spixfer        waitms
  flashinfo      fpmatch        hostevent      sysinfo
  flashread      gettime        md             sysjump
  flashwp        gpioget        panicinfo      syslock
  flashwrite     gpioset        reboot         taskinfo
HELP LIST = more info; HELP CMD = help on CMD.
```

Start a fingerprint enrollment:

```
> fpenroll
```

### Measuring Power {#measure-power}

The Dragonclaw reference board has an onboard INA that monitors the voltage and
power draw of the MCU and FP Sensor independently.

Signal Name     | Description
--------------- | -------------------------------------
`pp3300_dx_mcu` | 3.3V supplying the MCU
`pp3300_dx_fp`  | 3.3V supplying the fingerprint sensor
`pp1800_dx_fp`  | 1.8V supplying the fingerprint sensor

You can monitor all power and voltages by using the following command:

```bash
(chroot) $ watch -n0.5 dut-control pp3300_dx_mcu_mv pp3300_dx_fp_mv pp1800_dx_fp_mv pp3300_dx_mcu_mw pp3300_dx_fp_mw pp1800_dx_fp_mw
```

You can get a summary of the power over `N` seconds with:

```bash
(chroot) $ dut-control -t N pp3300_dx_mcu_mv pp3300_dx_fp_mv pp1800_dx_fp_mv pp3300_dx_mcu_mw pp3300_dx_fp_mw pp1800_dx_fp_mw
```

When measuring the power, make sure that any debuggers are disconnected. The
most reliable way to make sure it is disconnected is to physically disconnect
the debugger and servo_micro from the board. Then re-attach servo_micro and
restart `servod`.

<!-- mdformat off(b/139308852) -->
*** note
The `_mv` suffix denotes millivolt and `_mw` suffix denotes milliwatt.
***

*** note
See [Power Measurement Documentation] for more information.
***
<!-- mdformat on -->

### Toggling Hardware Write Protect

When using a fingerprint development board connected to servo, you can toggle
hardware write protect for testing.

**NOTE**: `servod` must be running.

Check the state of hardware write protect:

```bash
(chroot) $ dut-control fw_wp_en
```

Enable hardware write protect:

```bash
(chroot) $ dut-control fw_wp_en:on
```

Disable hardware write protect:

```bash
(chroot) $ dut-control fw_wp_en:off
```

### Contributing Changes

#### Using Gerrit and git

If you’re not familiar with `git`, Gerrit (code review) and `repo`, here are
some docs to help you get started:

*   [Git and Gerrit Intro for Chromium OS]: Useful to get started as quickly as
    possible, but does not explain how `git` works under the hood.
*   [Set your editor]: Use your favorite editor when writing `git` commit
    messages.
*   [Chromium OS Contributing Guide]: Detailed overview of contributing changes
    to Chromium OS and the workflow we use.
*   [Git: Concepts and Workflow]: Good overview of how `git` actually works.
*   [Gerrit: Concepts and Workflow]: Good overview of how Gerrit works; assumes
    you understand `git` basics.
*   [Life of a patch]: Android workflow, but similar to ChromeOS.

The Gerrit dashboard that will show your pending reviews (and ones we have for
you):

*   [Public Gerrit]
*   [Internal Gerrit]

#### Registering for a chromium.org *Internal* Account

If your partnership agreement requires non-public code sharing you will need to
register for an account on the [Internal Gerrit]. Refer to the
[Gerrit Credentials Setup] page for details. Once you register for an internal
account, your contact at Google can make sure you have the necessary permissions
to access the private repository.

<!-- mdformat off(b/139308852) -->
*** note
**NOTE**: In order to use a private repository you will have to manually add it
to the repo manifest file before running `repo sync`. Check with your contact
at Google for the exact values to use below:

**`(outside) $ ~/chromiumos/.repo/manifests/default.xml`**

```xml
<project remote="cros-internal"
         path="CHECK WITH GOOGLE"
         groups="firmware"
         name="CHECK WITH GOOGLE" />
```

**`(outside) $ ~/chromiumos/.repo/manifests/remote.xml`**

```xml
<remote name="cros-internal"
        fetch="https://chrome-internal.googlesource.com"
        review="https://chrome-internal-review.googlesource.com" />
```
***
<!-- mdformat on -->

### Tracking Issues and Communication

Development issue tracking and communication is done through the
[Partner Issue Tracker]. You will use your [Partner Domain] account to access
the [Partner Issue Tracker]. If you do not already have a [Partner Domain]
account, please request one from your Google contact.

In order to make sure that you receive email notifications for issues, please
make sure that you [set up email forwarding] and set your
[notification settings] appropriately. Communication should primarily be done
through the [Partner Issue Tracker] and not email so that it can be more easily
tracked by multiple people and a record is preserved for posterity.

[Partner Issue Tracker]: https://developers.google.com/issue-tracker/guides/partner-access
[Partner Domain]: https://developers.google.com/issue-tracker/guides/partner-domains
[set up email forwarding]: https://developers.google.com/issue-tracker/guides/partner-domains#email_forwarding
[notification settings]: https://developers.google.com/issue-tracker/guides/set-notification-preferences

## Working with Chromebooks

Chromebooks have an FPMCU (e.g., Dragonclaw) board attached to the motherboard.
You can use the device to run `ectool` commands and test the fingerprint sensor
from the UI.

### Developer Mode and Write Protection

Make sure that your fingerprint-equipped ChromeOS device is in [developer mode]
with a *test* image flashed and [hardware write protection] disabled. Using the
test image will allow you to SSH into the device and disabling hardware write
protection allows you to have full access to flashing the FPMCU firmware.

See [Installing Chromium] for details on flashing test images and enabling
[developer mode].

### Connecting

In general, most of our development is done by connecting to the DUT (device
under test) via SSH. We usually connect the DUT to ethernet (e.g., via USB-C to
Ethernet converter), but WiFi should also work (assuming corporate firewall
restrictions don’t block SSH port 22). To get the IP address, tap the
battery/time icon in the lower right corner. Then tap on “Ethernet” followed by
the gear icon in the upper right.

```bash
(chroot) $ ssh root@<IP_ADDRESS>
Password: test0000
```

Once you have SSH’ed into the DUT, you should be able to run `ectool` commands.

**Example**: Capture a "test_reset" image from the sensor and write it to a
[PNM] file (viewable with the ImageMagick `display` command):

```bash
(device) $ ectool --name=cros_fp fpmode capture test_reset; ectool --name=cros_fp waitevent 5 500; ectool --name=cros_fp fpframe > /tmp/test_reset.pnm
```

Alternatively, you can access a shell via the UI on device by pressing
`CTRL+ALT+F2` (third key on top row). Log in with `root` and `test0000`.

### Flashing FPMCU from DUT

Copy the firmware to the DUT:

```bash
(chroot) $ scp ./build/bloonchipper/ec.bin <DUT_IP>:/tmp/ec.bin
```

From the DUT, flash the firmware you copied:

```bash
(device) $ flash_fp_mcu /tmp/ec.bin
```

## Commit-queue Prototype Environment

![CQ Prototype Environment]

![FPMCU devboard environment v2 with satlab]

## Troubleshooting

### Verify that servo and debugger are connected to USB {#servo-connected}

Check whether servo is enumerating on USB. If you are using a debugger
(Lauterbach, J-Link, etc), also check to make sure it enumerates. Depending on
the debugger being used, it may need to be powered with an external power
supply.

```bash
(chroot) $ lsusb

Bus 002 Device 003: ID 0897:0004 Lauterbach  # ← This is my Lauterbach (debugger)
Bus 001 Device 013: ID 18d1:5002 Google LLC # ← This is servo
```

### "No servos found" when running servod

If you get the following message, make sure that
[servo is connected to USB](#servo-connected). You may also want to try
restarting your machine (or VM).

```bash
(chroot) $ sudo servod --board=bloonchipper
2019-04-12 14:53:42,236 - servod - INFO - Start
2019-04-12 14:53:42,270 - servod - ERROR - No servos found
```

### Losing characters in servo UART console

Make sure that this interface is disabled:

```bash
(chroot) $ dut-control usbpd_ec3po_interp_connect:off
```

### FPMCU console commands

*   Once the console is working you can use `help` to see the commands.
*   There should be fingerprint commands that start with `fp` (see `fpsensor.c`
    in the [EC] code).

<!-- Links -->

[EC]: https://chromium.googlesource.com/chromiumos/platform/ec
[ectool_servo_spi]: https://chromium.googlesource.com/chromiumos/platform/ec/+/HEAD/util/comm-servo-spi.c#15
[servo]: https://chromium.googlesource.com/chromiumos/third_party/hdctools/+/HEAD/README.md
[developer mode]: https://chromium.googlesource.com/chromiumos/docs/+/HEAD/debug_buttons.md#firmware-keyboard-interface
[hardware write protection]: https://chromium.googlesource.com/chromiumos/platform/ec/+/HEAD/docs/write_protection.md
[have the prerequisites]: https://chromium.googlesource.com/chromiumos/docs/+/HEAD/developer_guide.md#Prerequisites
[get the source]: https://chromium.googlesource.com/chromiumos/docs/+/HEAD/developer_guide.md#get-the-source
[enter the `chroot`]: https://chromium.googlesource.com/chromiumos/docs/+/HEAD/developer_guide.md#building-chromium-os
[Chromium OS Contributing Guide]: https://chromium.googlesource.com/chromiumos/docs/+/HEAD/contributing.md
[Servo Micro Info]: https://chromium.googlesource.com/chromiumos/third_party/hdctools/+/HEAD/docs/servo_micro.md
[Set your editor]: https://chromium.googlesource.com/chromiumos/docs/+/HEAD/developer_guide.md#Set-your-editor
[Life of a patch]: https://source.android.com/setup/contribute/life-of-a-patch
[Git: Concepts and Workflow]: https://docs.google.com/presentation/d/1IQCRPHEIX-qKo7QFxsD3V62yhyGA9_5YsYXFOiBpgkk/
[Gerrit: Concepts and Workflow]: https://docs.google.com/presentation/d/1C73UgQdzZDw0gzpaEqIC6SPujZJhqamyqO1XOHjH-uk/
[Public Gerrit]: https://chromium-review.googlesource.com
[Power Measurement Documentation]: https://chromium.googlesource.com/chromiumos/third_party/hdctools/+/HEAD/docs/power_measurement.md
[Internal Gerrit]: https://chrome-internal-review.googlesource.com
[Gerrit Credentials Setup]: https://www.chromium.org/chromium-os/developer-guide/gerrit-guide
[Micro USB Cable]: https://www.monoprice.com/product?p_id=9762
[PNM]: https://en.wikipedia.org/wiki/Netpbm_format
[Git and Gerrit Intro for Chromium OS]: https://chromium.googlesource.com/chromiumos/docs/+/HEAD/git_and_gerrit_intro.md
[Installing Chromium]: https://chromium.googlesource.com/chromiumos/docs/+/HEAD/developer_guide.md#installing-chromium-os-on-your-device
[FPMCU documentation]: ./fingerprint.md
[Fingerprint Debugging]: ./fingerprint-debugging.md
[dragonclaw schematics]: ../schematics/dragonclaw

<!-- Images -->

[Servo Micro]: ../images/servo_micro.jpg
[Servo Micro with Dragonclaw]: ../images/servomicro_dragonclaw.jpg
[Dragonclaw board]: ../images/dragonclaw_v0.3.jpg
[Dragonclaw servo fix diagram]: ../images/dragonclaw_servo_fix.jpg
[Icetower board]: ../images/icetower_v3.jpg
[Quincy board]: ../images/quincy_v2.jpg

<!-- If you make changes to the docs below make sure to regenerate the JPEGs by
     appending "export/pdf" to the Google Drive link. -->

<!-- https://docs.google.com/drawings/d/1YhOUD-Qf69NUdugT6n0cX7o7CWvb5begcdmJwv7ch6I -->

[Dragonclaw Rev 0.2 1.8V Rework]: https://github.com/coreboot/chrome-ec/blob/HEAD/docs/images/dragonclaw_rev_0.2_1.8v_load_switch_rework.pdf

<!-- https://docs.google.com/drawings/d/1w2qbb4AsSxY-KTK2vXZ6TKeWHveWvS3Dkgh61ocu0wc -->

[CQ Prototype Environment]: ../images/CQ_Prototype_Environment_Sinclair.jpg

<!-- https://docs.google.com/drawings/d/1zPQEF65doEVDCL0sXHb0tACpq0GSP1ZH5c2vEIjpC98 -->

[FPMCU devboard environment v2 with satlab]: ../images/FPMCU_devboard_environment_v2_with_Satlab.jpg
