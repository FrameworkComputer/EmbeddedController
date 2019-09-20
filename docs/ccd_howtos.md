# CCD How-tos
This doc contains tutorials for using CCD. These tutorials only cover using GSC
CCD. Some use cases will be very similar to using CCD from ryu, servo micro, or
servo v4, but these guides are not guaranteed to work for them. More detailed
instructions on how to use different parts of CCD are in the
[GSC CCD doc](case_closed_debugging_cr50.md).

[TOC]

---
## How to Use SuzyQ
This goes through the steps to connect SuzyQ and start using CCD.

### Requirements

*   A [SuzyQ]. If you don't have one, they're sold at [sparkfun]
*   A ChromeOS device that supports CCD.

### Steps

1.  **Charge your chromebook.** Suzyq can't charge your device. If it's not
    charged, the device may run out of power while debugging.

2.  **Connect the type A side of Suzyq to your workstation.**

3.  **Connect the type C part of your Suzyq to your chromebook.**

4.  **Verify the CCD device exists.**

    *   **Look for a device with the right vid:pid.** Cr50 vid:pid is 18d1:5014.
        You can use lsusb to check that it shows up.

                > lsusb -vd 18d1:5014

    *   **Debug connection issues**. If the device doesn't show up, disconnect
        suzyq from the DUT and either flip it or plug it into the other port. If
        your device has 2 type c ports, there are 4 ways to connect suzyq. Only
        one works.

        *   **Port:** The DUT only supports CCD on one type C port. Try the
            other port if CCD doesn't show up.

        *   **Orientation:** Suzyq is orientation dependent, so it may be on the
            correct port, but it needs to be flipped.

5.  **Check basic CCD functionality**. After the CCD device shows up, the cr50, ec,
    and ap consoles should show up in /dev/ttyUSB\*

    *   Search for console names.

                > ls /dev/ttyUSB*

    *   If you run the `ls` command before and after connecting suzyq, then the
        new devices should be the CCD consoles. The consoles are ordered. Cr50
        should be the lowest ttyUSB device, then AP, and EC should have the
        highest number. Running `ver` on all of them could also let you know
        which one is which if you don't want to remember the order.

    *   Open the console.

                > minicom -D /dev/ttyUSB0

    *   AP and EC consoles may be read-only depending on the CCD state. See the
        [Setup CCD] instructions to enable them. Being able to use the cr50
        console is a good enough sign that your Suzyq setup is ok.

[Setup CCD]: ./case_closed_debugging_cr50.md#CCD-Setup
[sparkfun]: https://www.sparkfun.com/products/14746
[SuzyQ]: https://chromium.googlesource.com/chromiumos/third_party/hdctools/+/refs/heads/master/docs/ccd.md#suzyq-suzyqable
