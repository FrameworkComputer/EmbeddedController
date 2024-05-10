# Fingerprint Firmware Testing Instructions for Partners

This document is intended to help partners (sensor vendors, MCU vendors, etc)
run the ChromeOS fingerprint team's firmware tests, as part of the AVL process.
The document assumes that you‘re using Linux to do the development; preferably a
recent version of Ubuntu or Debian. It may be possible to use a virtual machine,
but that is not a configuration we test.

[TOC]

## Hardware Requirements

You will need a Chromebook with the fingerprint sensor and fingerprint MCU
(FPMCU), and a [servo debugger].

### Chromebook with fingerprint sensor

The Chromebook needs to be in [developer mode] and running a test image so that
the test can ssh into it. The fingerprint firmware tests will run a series of
bash commands, including flashing the FPMCU firmware and rebooting the
Chromebook. You do not need [CCD] because servo will handle the firmware write
protection for you.

### Servo

Servo is a general purpose debug board used in many automated tests in Chromium
OS. Among other things, servo enables the tests to toggle hardware write
protect.

While there are multiple versions of servo, for firmware tests we strongly
recommend [Servo V4] as that's the simplest and most often used in autotests.
This document will assume you are using Servo V4.

### Hardware Setup

*   Connect the "HOST" side of Servo V4 to your host machine (which should have
    a Chromium OS chroot).
*   Connect the other side of Servo V4 to a USB port on the Chromebook with
    fingerprint sensor.
*   Connect the "DUT POWER" side of Servo V4 to power supply.
*   Make sure the USB cable from the host machine to Servo V4 is in data
    transfer mode (i.e. if there's an LED, it should be yellow instead of
    green).
*   Make sure the you can ssh into the Chromebook from the chroot on the host
    machine.

## Software Setup

### Get the Chromium OS source code.

*   First, make sure you [have the prerequisites].
*   Then [get the source].

### Build the autotest codebase

```bash
# from a terminal on your machine
(outside chroot) $ cd ~/chromiumos/src

# enter the chroot
(outside chroot) $ cros_sdk
```
### Servod setup

Follow [servod outside chroot] instructions.

### Start servod

```bash
(outside) $ start-servod --channel=release --board=$BOARD -p 9999
```

At this point the servod daemon should be running and listening to port 9999.
If it isn't, check the hardware connection.

## Run a Single Fingerprint Firmware Test

Use another terminal and enter the chroot like before:

```bash
(outside chroot) $ cd ~/chromiumos/src
(outside chroot) $ cros_sdk
```

To run a single test, use this command in your chroot:

```bash
test_that --board=<BOARD> <IP> <test name>
```

For example:

```bash
tast run <IP> firmware.FpReadFlash
```

## Run the Entire Fingerprint Firmware Test Suite

To run the entire suite, use this command in your chroot:

```bash
tast run <IP> '("group:fingerprint-cq")'
```

<!-- Links -->

[servo debugger]: https://chromium.googlesource.com/chromiumos/third_party/hdctools/+/HEAD/docs/servo.md
[developer mode]: https://chromium.googlesource.com/chromiumos/docs/+/HEAD/developer_mode.md
[CCD]: https://chromium.googlesource.com/chromiumos/platform/ec/+/refs/heads/cr50_stab/docs/case_closed_debugging.md
[Servo V4]: https://chromium.googlesource.com/chromiumos/third_party/hdctools/+/HEAD/docs/servo_v4.md
[have the prerequisites]: https://chromium.googlesource.com/chromiumos/docs/+/HEAD/developer_guide.md#Prerequisites
[get the source]: https://chromium.googlesource.com/chromiumos/docs/+/HEAD/developer_guide.md#get-the-source
[servod outside chroot]: https://chromium.googlesource.com/chromiumos/third_party/hdctools/+/HEAD/docs/servod_outside_chroot.md
