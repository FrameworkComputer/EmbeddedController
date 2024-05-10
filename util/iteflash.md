# Reflashing an ITE EC

This doc: [http://go/cros-ite-ec-reflash](https://goto.google.com/cros-ite-ec-reflash)
<br>
First written: 2019-04-02
<br>
Last updated: 2024-04-17

Familiarity with [Chromium OS](https://www.chromium.org/chromium-os) and
[Embedded Controller (EC)](../README.md) development is assumed.

[TOC]

## Background

### Terminology

**ITE EC** refers to the [ITE](http://www.ite.com.tw/)
[IT8320](http://www.ite.com.tw/en/product/view?mid=96)
[Embedded Controller (EC)](https://en.wikipedia.org/wiki/Embedded_controller)
microcontroller when used as a Chromium OS / Chrome OS EC.

**CrOS** refers to Chromium OS, Chrome OS, or both, depending on the context.
The distinction between Chromium OS and Chrome OS is largely immaterial to this
document.

**Servo** refers to a debug board providing direct debug access to various
circuits on a Chrome OS device motherboard. As of this writing, the most common
[servos](https://www.chromium.org/chromium-os/servo) used by CrOS developers are
[CR50 (CCD)](https://www.chromium.org/chromium-os/ccd),
[Servo Micro](https://www.chromium.org/chromium-os/servo/servomicro), and
[Servo v2](https://www.chromium.org/chromium-os/servo/servo-v2). (Note that
[Servo v4](https://www.chromium.org/chromium-os/servo/servov4) is **not** a
Servo in this sense. It is a USB hub with a microcontroller that proxies Servo
functionality from either CR50 or Servo Micro.) See also
[Case-Closed Debug in Chromebooks and Servo Micro](https://chromium.googlesource.com/chromiumos/platform/ec/+/HEAD/board/servo_micro/ccd.md).

### How ITE EC reflashing works

An ITE EC is reflashed using a Servo by:

1.  Sending special non-I2C waveforms over its I2C clock and data lines, to
    enable a debug mode / direct firmware update (DFU) mode.

1.  Communicating with it using I2C, including transferring the actual EC image
    over I2C.  The ITE EC will only respond over I2C after receiving the special
    waveforms.

### Further reading

* [ITE EC firmware reflashing via Servo: How it works](../docs/ite-ec-reflashing.md)
* Googlers, and Partners involved in ITE EC projects *only*:
[The State of ITE CrOS EC Reflashing](https://docs.google.com/document/d/1fs29eBvwKrOWYozLZXTg7ObwAO5dyM4Js2Vq301EwAU/preview)
  * That document is not public, do not request access if you lack it.

## How to reflash

### Prerequisites for CR50 CCD

This section applies whether using CR50 CCD via
[Servo v4](https://www.chromium.org/chromium-os/servo/servov4) or
[SuzyQ aka SuzyQable](https://www.sparkfun.com/products/14746).

CR50 MP minimum firmware version: `0.3.15`
<br>
CR50 pre-PVT minimum firmware version: `0.4.15`

Googlers, to upgrade CR50 firmware if needed see
[How to use CCD on CR50](https://docs.google.com/document/d/1MqDAoBsmGTmrFi-WNOoC5R-UFeuQK37_9kaEdCFU8QE/preview).
That document is not public, do not request access if you lack it.

The CR50 CCD capabilities must be set to `always`. To achieve this:

1.  Open CCD.
    *   root shell: `$ gsctool -o`
    *   CR50 console: `ccd open`
1.  Reset CCD to `factory` mode.
    *   CR50 console: `ccd reset factory`

### Prerequisites for Servo Micro

This section applies whether the
[Servo Micro](https://www.chromium.org/chromium-os/servo/servomicro) is
connected directly to your development host, or through a
[Servo v4](https://www.chromium.org/chromium-os/servo/servov4).

Servo Micro minimum firmware version: `servo_micro_v2.3.5`

To upgrade Servo Micro firmware if needed:

1.  Enter the chroot.
    *   `$ cros_sdk`
1.  Run servo_updater.
    *   `$ sudo servo_updater --board=servo_micro`

If that still results in too old of a firmware version, use `repo sync` and
`update_chroot` to update your CrOS development environment, then try again.

### Common reflash instructions

These instructions apply when using any kind of Servo, including those with no
special prerequisites (such as
[Servo v2](https://www.chromium.org/chromium-os/servo/servo-v2) with its Yoshi
flex cable connected to the DUT).

1.  Enter the chroot (for servod).
    *   `$ cros_sdk --no-ns-pid`
1.  Start servod.
    *   `$ sudo servod --board=<servod_board_name>`
    *   For some boards the servod board name is different than the EC codebase
        board name used below!
1.  Enter the chroot (for flash_ec).
    *   `$ cros_sdk`
1.  Build the EC image for your board.
    *   `$ cd ~/trunk/src/platform/ec`
    *   `$ board=<board_name>`
    *   `$ make -j BOARD="$board"`
1.  Run flash_ec from the util directory.
    *   `$ util/flash_ec --board="$board" --image=build/"$board"/ec.bin`

## CR50 CCD sans servod alternative {#ccd-sans-servod}

This section applies whether using CR50 CCD via
[Servo v4](https://www.chromium.org/chromium-os/servo/servov4) or
[SuzyQ aka SuzyQable](https://www.sparkfun.com/products/14746).

When using CR50 CCD, it is possible to reflash without servod, which _must not_
be running when using this method.

1.  Enter the chroot.
    *   `$ cros_sdk`
1.  Build the EC image for your board.
    *   `$ cd ~/trunk/src/platform/ec`
    *   `$ board=<board_name>`
    *   `$ make -j BOARD="$board"`
1.  Get the serial number of your CR50 or Ti50.
    *   `lsusb -d 18d1:504a -v | grep iSerial`
    *   `lsusb -d 18d1:5014 -v | grep iSerial`
1.  Run iteflash from the build/host/util directory.
    *   `$ build/host/util/iteflash --i2c-interface=usb
        --serial 12018039-4c59c752
        --send-waveform=1 --erase --write=build/"$board"/ec.bin`

WARNING: The `--i2c-mux` flag is only required for some ITE EC boards. For
boards without an I2C mux between CR50 and the EC, that flag _must not_ be
specified. (This is handled for you when using `flash_ec` + `servod` because the
latter has knowledge of which boards are expected to have the I2C mux.)
