# ITE EC firmware reflashing via Servo: How it works

This doc: [http://go/cros-ite-reflash-design](https://goto.google.com/cros-ite-ec-reflash-design)
<br>
First written: 2022-08-15
<br>
Last updated: 2022-08-24

Familiarity with [Chromium OS](https://www.chromium.org/chromium-os) and
[Embedded Controller (EC)](../README.md) development is assumed.

[TOC]

## Background

### Other documents
* [Reflashing an ITE EC](../util/iteflash.md)
* Googlers, and Partners involved in ITE EC projects only:
  [The State of ITE CrOS EC Reflashing](https://goto.google.com/cros-ite-ec-reflash-state)
  * That document is not public, do not request access if you lack it.
* `i2c-pseudo` [README](../extra/i2c_pseudo/README)
* `i2c-pseudo` [Documentation.txt](../extra/i2c_pseudo/Documentation.txt)

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
(FTDI FT4232HL).

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

1.  When `servod` starts, it creates a pseudo I2C adapter in Linux for every
    servo I2C bus it controls, if the `i2c-pseudo` module is loaded.
    * This pseudo I2C adapter can be used on the host system as if it were a
      native I2C bus, including from userspace if the `i2c-dev` module is
      loaded.
    * For more information on the `i2c-pseudo` module see
      [Reflashing an ITE EC](../util/iteflash.md), as well as `i2c-pseudo`'s
      [README](../extra/i2c_pseudo/README) and
      [Documentation.txt](../extra/i2c_pseudo/Documentation.txt).
1.  `flash_ec` issues a `servod` command for the DUT controller servo to send
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
      1.  The `CR50` firmware function for sending the special waveforms is
          invoked by a special I2C message, not a console command.
      1.  `CR50` must reboot itself to perform the special waveforms. During
          normal operation `CR50` has deliberate clock jitter which would
          prevent accurately preforming the waveforms. This jitter cannot
          safely be disabled, except on reset, and only while the `AP` is held
          in reset.
    * [Future] If we were to support this control flow with `Servo v2`, the
      cleanest way would be to move the FTDI-based bit-banging of the
      special waveforms from `iteflash` into `servod` itself, as a C/C++
      extension, so that `flash_ec` can trigger it with a `servod` command the
      same as for other servo types. This would allow removing the hack in
      `servod` to relinquish control of the `Servo v2` FTDI USB interface.
      * Proof-of-concept [CL:1522847](https://crrev.com/c/1522847) adds support
        for using Servo v2 via `servod`. However as of this writing that CL
        ([patchset 14](https://crrev.com/c/1522847/14)) only changes the I2C
        communication path, it does NOT move the special waveforms into
        `servod`, which is needed to remove the `servod` I2C interface
        close + reopen hack and fully merge the Servo v2 ITE EC reflashing into
        this new control flow.
1.  `flash_ec` asks `servod` for the local Linux i2c-dev path of the
    DUT Controller Servo's DUT-connected I2C interface (which is backed by
    `servod` itself via the `i2c-pseudo` module).
1.  `flash_ec` invokes `iteflash`, passing it the i2c-dev path given by
    `servod`.
1.  `iteflash` performs the EC firmware update via the i2c-dev interface.

## Why `i2c-pseudo` and alternative implementations considered

Instead of using `i2c-dev` Linux I2C interfaces, `iteflash` could communicate
directly with `servod` using a custom protocol. This would make `iteflash`
dependent on `servod` and whatever custom protocol we come up with, as there is
no standard userspace<->userspace I2C interface to implement.

In the future we may choose to implement Servo I2C interfaces as actual
host-side Linux drivers, which `servod` would use via `i2c-dev`
(which it supports already!). Since the `flash_ec` and `iteflash` portions of
this process are built around `i2c-dev` now, they should continue working with
no changes needed for this scenario.

Why bother with i2c-pseudo at all then? Why not go straight to reimplementing
the Servo I2C interfaces as new Linux I2C adapter drivers, instead of
implementing the new `i2c-pseudo` driver?

Rearchitecting the Servo I2C interfaces is not something to be considered
lightly, and not worthwhile just for ITE EC reflashing. By staying with the
existing `servod` Servo I2C implementations we have not introduced any
dependency on new kernel modules for *existing* `servod` functionality. Only
the new ITE EC reflashing functionality depends on `i2c-pseudo`. As with
`i2c-pseudo` we would need to rely on out-of-tree kernel module distribution
for these new Servo I2C modules until eventual upstream acceptance +
percolation down to distribution Linux kernels, with no guarantee of acceptance
for our obscure Servo hardware. Depending on a new kernel module for this one
new function of ITE EC reflashing is one thing. Requiring new modules for all
`servod` use would be quite another. Realistically we would need to maintain
fallback code in `servod` to use its existing internal Servo I2C interface
implementations when the kernel ones aren't available, but that has a
maintenance cost too. These same issues would be faced with every new
generation of Servo, so this broad Servo + `servod` architectural change is not
something to be considered lightly or just for ITE EC reflashing.

`i2c-pseudo` has potential uses in the CrOS ecosystem beyond ITE EC reflashing.
A big one is mocking I2C interfaces for driver and system tests. There is the
longstanding `i2c-stub` module for this purpose, but its functionality is
limited compared to `i2c-pseudo`, not all I2C device behavior can be modeled
with `i2c-stub`. Also by having the `servod` I2C pseudo interfaces, one can
conveniently use the standard Linux I2C command line tools
(i2cget(8), i2cset(8), i2ctransfer(8), etc) for interfacing with Servo and DUT
I2C devices. While it is unlikely that i2c-pseudo will have any use in CrOS
itself, it is expected to have further uses in both developer tooling and
code tests.
