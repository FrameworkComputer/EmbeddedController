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

---
## I Just Want to Disable Write Protect
Cr50 has a couple of ways to remove write protect. The biggest difference in the
process is whether or not you want to open the case and whether or not you need
write protect disable to be permanent.

**Opening CCD might require the AP can boot. If you're relying on CCD to recover
a bricked machine, you may want to do the optional CCD setup steps before
flashing RO firmware.**

### Process if You're Okay Opening Case
Cr50 will disable write protect if you remove the battery.

#### Steps

1.  **Open the Case.**

2.  **Remove the battery.** Cr50 disables write protect when the battery is
    disconnected. On chromeboxes you need to remove the write protect screw.

3.  (bob only) remove the write protect screw. Bob uses cr50 and a write protect
    screw. Cr50 has to disable write protect and the screw has to be removed for
    write protect to be disabled.

4.  (optional) Check write protect is disabled from the AP.

                AP > flashrom --wp-status

5.  (optional) Reconnecting the battery will reenable write protect. You can
    disable SW write protect if you want to be able to rewrite RO firmware
    without needing to keep the battery disconnected.

                AP > flashrom -p host --wp-disable

6.  **(recommended) Run some basic commands to setup CCD.** It's really easy to
    open cr50 with the battery removed. You might want to setup CCD while you
    already have the case open and the battery is disconnected. Doing these
    extra CCD setup steps may make it easier to recover a bricked device using
    CCD. These will require SuzyQ.

    *   **Open Cr50** from the AP or Cr50 console. This will happen immediately.


                AP > gsctool -a -o

        or

                Cr50 > ccd open



    *   **Setup capabilities.** This can only be done from the Cr50 console.

        Enable flashing the AP/EC CCD open.

                Cr50 > ccd set OverrideWP Always
                Cr50 > ccd set FlashAP Always
                Cr50 > ccd set FlashEC Always

        Enable opening Cr50 without booting the AP.

                Cr50 > ccd set OpenNoDevMode Always
                Cr50 > ccd set OpenFromUSB Always

### Process With Case Closed
Full instructions are at [Setup CCD], but here are are the basic steps. If
you're unsure about a step here you should take a look at the [Setup CCD] doc.
It goes into a lot more detail.

#### Requirements

*   A [SuzyQ]. Cr50 console access is required to disable write protect.
*   The device needs to be able to boot.
*   The GBB\_FLAG\_FORCE\_DEV\_SWITCH\_ON GBB flag is cleared.

#### Steps
1.  **Open CCD.** Here's the most generic way to open ccd. For the full open
    options see [Setup CCD].

    *   **[Enter dev mode](case_closed_debugging_cr50.md#enter-dev-mode).**
        These are clamshell instructions for other types of chromeOS devices
        refer to the full setup doc.

        *   Boot into recovery by pressing the power button, refresh, and
            escape.

        *   At the recovery screen press Ctrl+D and enter.

    *   **Use gsctool to open Cr50 from the AP.** Press the power button when
        prompted. This will take ~5 minutes.

                AP > gsctool -a -o

2.  **[Connect](#How-to-Use-SuzyQ) to the Cr50 console** using SuzyQ or servo
    v4.

                > minicom -D /dev/ttyUSB0

3.  **Disable write protect** using Cr50 [wp console command].

    cmd to disable until cr50 reboots:

                cr50 > wp disable

    cmd to disable it indefinitely:

                cr50 > wp disable atboot


4.  (optional) Check write protect is disabled. ccd open takes the AP out of dev
    mode, so you can reenter dev mode and check the wp status from the AP or you
    can use ccd to check.

    From AP (after reentering dev mode):

                AP > flashrom --wp-status

    Using CCD:

                from chroot > flashrom -p raiden_debug_spi:target=AP --wp-status

5.  **(recommended) Setup capabilities**, so you can flash the device or open
    ccd without being able to boot the AP.

    Make flashing the AP/EC accessible without opening CCD

                Cr50 > ccd set OverrideWP Always
                Cr50 > ccd set FlashAP Always
                Cr50 > ccd set FlashEC Always

    Enable opening Cr50 without booting the AP

                Cr50 > ccd set OpenNoDevMode Always
                Cr50 > ccd set OpenFromUSB Always

6.  **(recommended) [Disable SW WP]** to flash RO firmware if your board has
    issues disabling HW WP with the AP off.

                AP > flashrom -p host --wp-disable

[Setup CCD]: ./case_closed_debugging_cr50.md#CCD-Setup
[sparkfun]: https://www.sparkfun.com/products/14746
[SuzyQ]: https://chromium.googlesource.com/chromiumos/third_party/hdctools/+/refs/heads/master/docs/ccd.md#suzyq-suzyqable
[wp console command]: ./case_closed_debugging_cr50.md#WP-control
[Disable SW WP]: ./case_closed_debugging_cr50.md#AP-Off
