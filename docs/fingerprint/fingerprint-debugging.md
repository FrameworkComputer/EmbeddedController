# Fingerprint Debugging

This document describes how to attach a debugger with SWD in order to debug the
FPMCU with [`gdb`](#gdb) or to [flash the FPMCU](#flash).

[TOC]

## Overview

### SWD

`SWD` (Single Wire Debug) was introduced by ARM with the Cortex-M family to
reduce the pin count required by JTAG. JTAG requires 5 pins, but SWD can be done
with only 3 pins. Furthermore, one of the freed up pins can be repurposed for
tracing.

See [CoreSight Connectors] for details on the three standard types of connectors
used for JTAG and SWD for ARM devices.

## Hardware Required

*   JTAG/SWD Debugger Probe: Any debug probe that supports SWD will work, but
    this document assumes that you're using a
    [Segger J-Trace PRO for Cortex-M][J-Trace].
*   [Dragonclaw v0.3 Development board][FPMCU dev board] or
    [Icetower v0.1 Development board][FPMCU dev board].
*   [Servo Micro].

## Software Required

*   [JLink Software] \(when using [J-Trace] or other Segger debug probes). This
    is the only software required for flashing.
*   In order to perform breakpoint debugging, you will need a tool that supports
    connecting `gdbserver`. This document will assume [CLion] \(Googlers see
    [CLion for ChromeOS]) and was tested with `JLink v6.94a`. Alternatively, you
    can use [Ozone], a standalone debugger from Segger.

## JLink Software {#software}

Install the JLink software in the chroot with the following command:

```bash
(chroot) $ sudo emerge jlink
```

<!-- mdformat off(b/139308852) -->
*** note
**NOTE**: The above command will print out a message directing you to manually
download a tarball. You must follow these instructions for the installation to
be complete.
***
<!-- mdformat on -->

## Connecting SWD {#connect-swd}

### Dragonclaw v0.3

The connector for SWD is `J4` on Dragonclaw v0.3.

<!-- mdformat off(b/139308852) -->
*** note
**NOTE**: Pay attention to the location of pin 1 (red wire) in the
photos below so that you connect with the correct orientation.

`SW2` on the edge of Dragonclaw must be set to `CORESIGHT`.

If you want to connect a 20-Pin ARM Standard JTAG Connector (0.10" / 2.54 mm),
you can use the following [adapter][JTAG to SWD Adapter] and [cable][SWD Cable].
***
<!-- mdformat on -->

Dragonclaw v0.3 with 20-pin SWD (0.05" / 1.27mm) on J4. Only half the pins are connected. |
----------------------------------------------------------------------------------------- |
![Dragonclaw with 20-pin SWD]                                                             |

Dragonclaw v0.3 with 10-pin SWD (0.05" / 1.27mm) on J4. |
------------------------------------------------------- |
![Dragonclaw with 10-pin SWD]                           |

### Icetower v0.1

The connector for SWD is `J4` on Icetower v0.1.

`SW2` on Icetower must be set to `CORESIGHT` (not `SERVO`).

Icetower v0.1 with 20-pin SWD (0.05" / 1.27mm) on J4. |
----------------------------------------------------- |
![Icetower with 20-pin SWD]                           |

## Powering the Board {#power}

[Servo Micro] can provide both the 3.3V for the MCU and 1.8V for the sensor.

Run the following to start `servod`, which will enable power to these rails by
default:

```bash
(chroot) $ sudo servod --board=<BOARD>
```

where `<BOARD>` is the board you are working with
([`dartmonkey` or `bloonchipper`][fingerprint hardware]).

Theoretically, it's also possible to power through J-Trace, though the
[power pin] on J-Trace only outputs 5V, whereas the MCU runs at 3.3V and the
sensor runs at 1.8V. The pin is also not connected on the current designs.

## Flashing the FPMCU with JLink {#flash}

*   Install the [JLink Software](#software).
*   [Connect SWD](#connect-swd).
*   [Power the board with servo](#power).
*   Start the JLink server:

```bash
(chroot) $ cd ~/trunk/src/platform/ec
```

```bash
# JLinkRemoteServerCLExe will listen on port 19020 (among others) by default.
# This can be overridden with the -Port argument.
(chroot) $ sudo JLinkRemoteServerCLExe -select USB
```

You should see the following:

```bash
SEGGER J-Link Remote Server V6.94a
Compiled Jan 14 2021 11:52:48

'q' to quit '?' for help

Connected to J-Link with S/N 123456

Waiting for client connections...
```

*   Build the FPMCU image:

```bash
(chroot) $ cd ~/trunk/src/platform/ec
```

```bash
(chroot) $ make BOARD=<BOARD> -j
```

replacing `<BOARD>` with [`bloonchipper` or `dartmonkey`][fingerprint hardware].

*   Run the [`flash_jlink.py`] script:

```bash
(chroot) $ ~/trunk/src/platform/ec/util/flash_jlink.py --board <BOARD> --image ./build/<BOARD>/ec.bin
```

replacing `<BOARD>` with [`bloonchipper` or `dartmonkey`][fingerprint hardware].

## Using JLink gdbserver {#gdb}

Start the JLink gdbserver for the appropriate MCU type:

*   Dragonclaw / [Nucleo STM32F412ZG]: `STM32F412CG`
*   Icetower / [Nucleo STM32H743ZI]: `STM32H743ZI`

```bash
(chroot) $ JLinkGDBServerCLExe -select USB -device STM32F412CG -endian little -if SWD -speed auto -noir -noLocalhostOnly
```

You should see the port that `gdbserver` is running on in the output:

```bash
Connecting to J-Link...
J-Link is connected.
Firmware: J-Trace PRO V2 Cortex-M compiled Feb  5 2021 14:50:19
Hardware: V2.00
S/N: XXXXX
Feature(s): RDI, FlashBP, FlashDL, JFlash, GDB
Checking target voltage...
Target voltage: 3.30 V
Listening on TCP/IP port 2331    <--- gdbserver port
Connecting to target...
Connected to target
Waiting for GDB connection...
```

Configure your editor to use this [`.gdbinit`], taking care to set the correct
environment variables for the `BOARD` and `GDBSERVER` being used. For CLion, if
you want to use a `.gdbinit` outside of your `HOME` directory, you'll need to
[configure `~/.gdbinit`].

In your editor, specify the IP address and port for `gdbserver`:

```
127.0.0.1:2331
```

You will also want to provide the symbol files:

*   RW image: `build/<board>/RW/ec.RW.elf`
*   RO image: `build/<board>/RO/ec.RO.elf`

Also, since we're compiling the firmware in the chroot, but your editor is
running outside of the chroot, you'll want to remap the source code path to
account for this:

*   "Remote source" is the path inside the chroot:
    `/home/<username>/trunk/src/platform/ec`
*   "Local source" is the path outside the chroot:
    `${HOME}/chromiumos/src/platform/ec`

To debug with CLion, you will create a new [GDB Remote Debug Configuration]
called `EC Debug`, with:

*   `'target remote' args` (gdbserver IP and port from above): `127.0.0.1:2331`
*   `Symbol file` (RW or RO ELF): `/path/to/build/<board>/RW/ec.RW.elf`
*   `Path mapping`: Add remote to local source path mapping as described above.

After configuring this if you select the `EC Debug` target in CLion and
[click the debug icon][CLion Start Remote Debug], CLion and JLink will handle
automatically flashing the ELF file and stepping through breakpoints in the
code. Even if not debugging, this may help with your iterative development flow
since the JLink tool can flash very quickly since it performs a differential
flash. Note that you still need to recompile after making changes to the source
code before launching the debugger.

## Using Ozone

Ozone is a free standalone debugger provided by Segger that works with the
[J-Trace]. You may want to use it if you need more powerful debug features than
gdbserver can provide. For example, Ozone has a register mapping for the MCUs we
use, so you can easily inspect CPU registers. It can also be automated with a
scripting language and show code coverage when used with a [J-Trace] that is
connected to the trace pins on a board. Note that the Dragonclaw v0.3 uses an
STM32F412 package that does not have the synchronous trace pins, but the
[Nucleo STM32F412ZG] does have the trace pins.

[CoreSight Connectors]: http://www2.keil.com/coresight/coresight-connectors
[FPMCU dev board]: ./fingerprint-dev-for-partners.md#fpmcu-dev-board
[J-Trace]: https://www.segger.com/products/debug-probes/j-trace/models/j-trace/
[JLink Software]: https://www.segger.com/downloads/jlink/#J-LinkSoftwareAndDocumentationPack
[Servo Micro]: ./fingerprint-dev-for-partners.md#Servo-Micro
[JTAG to SWD Adapter]: https://www.adafruit.com/product/2094
[SWD Cable]: https://www.adafruit.com/product/1675
[Ozone]: https://www.segger.com/products/development-tools/ozone-j-link-debugger/
[CLion]: https://www.jetbrains.com/clion/
[CLion for ChromeOS]: http://go/clion-for-chromeos
[GDB Remote Debug Configuration]: https://www.jetbrains.com/help/clion/remote-debug.html#remote-config
[CLion Start Remote Debug]: https://www.jetbrains.com/help/clion/remote-debug.html#start-remote-debug
[Nucleo STM32F412ZG]: https://www.st.com/en/evaluation-tools/nucleo-f412zg.html
[Nucleo STM32H743ZI]: https://www.st.com/en/evaluation-tools/nucleo-h743zi.html
[`.gdbinit`]: /util/gdbinit
[configure `~/.gdbinit`]: https://www.jetbrains.com/help/clion/configuring-debugger-options.html#gdbinit-lldbinit
[power pin]: https://www.segger.com/products/debug-probes/j-link/technology/interface-description/
[fingerprint hardware]: ./fingerprint.md#hardware
[`flash_jlink.py`]: https://chromium.googlesource.com/chromiumos/platform/ec/+/HEAD/util/flash_jlink.py

<!-- Images -->

[Dragonclaw with 20-pin SWD]: ../images/dragonclaw_with_20_pin_swd.jpg
[Dragonclaw with 10-pin SWD]: ../images/dragonclaw_with_10_pin_swd.jpg
[Icetower with 20-pin SWD]: ../images/icetower_with_20_pin_swd.jpg
