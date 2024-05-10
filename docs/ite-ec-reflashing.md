# ITE EC firmware reflashing via Servo: How it works

This doc: [http://go/cros-ite-reflash-design](https://goto.google.com/cros-ite-ec-reflash-design)
<br>
First written: 2022-08-15
<br>
Last updated: 2024-04-17

Familiarity with [Chromium OS](https://www.chromium.org/chromium-os) and
[Embedded Controller (EC)](../README.md) development is assumed.

[TOC]

## Background

### Other documents
* [Reflashing an ITE EC](../util/iteflash.md)
* Googlers, and Partners involved in ITE EC projects only:
  [The State of ITE CrOS EC Reflashing](https://goto.google.com/cros-ite-ec-reflash-state)
  * That document is not public, do not request access if you lack it.

### Terminology

**EC** refers to an
[Embedded Controller](https://en.wikipedia.org/wiki/Embedded_controller)
(microcontroller).

**ITE EC** refers to the [ITE](http://www.ite.com.tw/)
[IT8320](http://www.ite.com.tw/en/product/view?mid=96)
[Embedded Controller (EC)](https://en.wikipedia.org/wiki/Embedded_controller)
microcontroller when used as a Chromium OS / Chrome OS EC.

**CrOS** refers to Chromium OS, Chrome OS, or both, depending on the context.
The distinction between Chromium OS and Chrome OS is largely immaterial to this
document.

**DUT Controller Servo** refers to a device that provides direct access
to various circuits on a Chrome OS device motherboard. As of this writing, the
most common DUT controller [servos](https://chromium.googlesource.com/chromiumos/third_party/hdctools/+/refs/heads/main/docs/servo.md) used by
CrOS developers are
[CR50 (CCD)](https://chromium.googlesource.com/chromiumos/third_party/hdctools/+/refs/heads/main/docs/ccd.md),
`C2D2`,
[Servo Micro](https://chromium.googlesource.com/chromiumos/third_party/hdctools/+/refs/heads/main/docs/servo_micro.md), and
[Servo v2](https://chromium.googlesource.com/chromiumos/third_party/hdctools/+/refs/heads/main/docs/servo_v2.md). (Note that
[Servo v4](https://chromium.googlesource.com/chromiumos/third_party/hdctools/+/refs/heads/main/docs/servo_v4.md) and
[Servo v4.1](https://chromium.googlesource.com/chromiumos/third_party/hdctools/+/refs/heads/main/docs/servo_v4p1.md) are **not**
DUT Controller Servos. They are Hub Servos, and are typically used in conjection with a DUT Controller Servo. Hub Servos are not directly involved in EC reflashing.)  See also
[Case-Closed Debug in Chromebooks and Servo Micro](https://chromium.googlesource.com/chromiumos/platform/ec/+/refs/heads/main/board/servo_micro/ccd.md).

**Servod** refers to a piece of software that runs on a USB host and provides
interfaces for controlling a Servo connected to the host as a USB device. See [servod](https://chromium.googlesource.com/chromiumos/third_party/hdctools/+/refs/heads/main/docs/servod.md).

## Core steps

Two things need to happen:

1.  Send special non-I2C waveforms over I2C clock and data lines to the ITE EC,
    to enable a debug mode in the EC where it will respond at a predefined
    I2C address as an I2C peripheral.
    * This debug mode is implemented by ITE in silicon and/or immutable
      firmware, it is not part of Chrome OS EC firmware. It is available even
      if Chrome OS RO+RW firmware on the EC is corrupted.

1.  Communicate with and control the ITE EC using its I2C-based debug mode. All
    signals on the I2C bus in question are now actual I2C, with the ITE EC
    acting as an I2C peripheral device. The EC firmware gets sent as I2C
    payload.
    * If the previous step is not successful, then the EC will not respond to
      I2C messages.

The DUT Controller Servo performs these steps.

## Control flow

[flash_ec](https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/util/flash_ec)
is the user interface for all Chrome OS device EC reflashing via Servos.
`servod` must be running to use `flash_ec`.

### Original control flow, for Servo v2 only

The original implementation of ITE EC reflashing via Servo is only compatible
with Servo v2, due to interfacing directly with its FTDI USB to MPSSE IC
(FTDI FT4232HL). There aren't any servo v2 devices available anymore.

1.  `flash_ec` tells `servod` to close its interface for controlling the
    `Servo v2` FTDI USB device.
    * This breaks the layering of `servod` as the interface through which
      servos are controlled, and is a maintenance + complexity burden to
      support in `servod`. No other servo I2C interfaces in `servod` support or
      need this functionality of relinquishing control.
1.  `flash_ec` invokes [iteflash](https://chromium.googlesource.com/chromiumos/platform/ec/+/refs/heads/main/util/iteflash.c).
1.  `iteflash` takes control of the `Servo v2` FTDI USB device.
1.  `iteflash` [bit-bangs](https://en.wikipedia.org/wiki/Bit_banging) the
    special waveforms using the `Servo v2` FTDI USB device.
1.  `iteflash` uses FTDI I2C functionality (not bit-banging) to talk I2C with
    the ITE EC, including sending the EC firmware as payload in I2C messages.
1.  `flash_ec` tells `servod` to reopen its `Servo v2` FTDI USB interface.

### New control flow through servod, for all other DUT controller servo types

1.  When `servod` uses I2C, it immediately unlocks the interface afterwards.
2.  `flash_ec` issues a `servod` command for the DUT controller servo to send
    the special waveforms.
    * For `Servo Micro` and `C2D2` all `servod` needs to do is issue a
      servo console command, `enable_ite_dfu`, which triggers a
      servo firmware function to perform the special waveforms.
      * The servo does not know what kind of DUT it is connected to, thus the
        `enable_ite_dfu` console commands are always available. The
        special waveforms will not do anything useful unless the DUT has
        an ITE EC.
    * `CR50` (CCD) is mostly the same, except:
      1.  CCD must be unlocked and the `ccd_i2c_en` CCD capability must be set
          to `Always`.
      2.  The `CR50` firmware function for sending the special waveforms is
          invoked by a special I2C message, not a console command.
      3.  `CR50` must reboot itself to perform the special waveforms. During
          normal operation `CR50` has deliberate clock jitter which would
          prevent accurately preforming the waveforms. This jitter cannot
          safely be disabled, except on reset, and only while the `AP` is held
          in reset.
3.  `flash_ec` asks `servod` for the serial number of the servo device.
4.  `flash_ec` invokes `iteflash`, passing it the serial given by
    `servod`.
5.  `iteflash` performs the EC firmware update via the USB i2c interface.
