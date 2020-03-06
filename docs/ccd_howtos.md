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
## Setup CCD for FAFT

These are the most generic instructions.

There are other ways to open ccd that may be faster, but they don't work for all
devices. You can see the other open methods in the ccd setup doc to find other
ways if this way doesn't work for you.

The entering dev mode instructions will be for clamshell devices. If your device
is not a clamshell, check out the [full dev mode instructions].

#### Requirements

*   A [Type-C Servo V4]. FAFT needs the ethernet and usb key to run. You can't
    run with suzyq.
*   A micro usb cable.
*   A chromeos PD charger.
*   A ChromeOS device that supports CCD.
*   Access to the AP console.
*   The device needs to be able to boot.
*   The GBB\_FLAG\_FORCE\_DEV\_SWITCH\_ON GBB flag is cleared.

### Steps


1.  **Charge your chromebook.** Servo V4 can charge your device, but it's good
    to charge it before setting up ccd. Servo v4 may encounter different issues
    if your device isn't charged.

2.  **Connect the type A side of the micro usb cable to your workstation.**

3.  **Connect the micro usb of your cable to "HOST" port of servo v4.**

4.  **Update Servo V4 from the chroot.**

                chroot > sudo emerge servo-firmware
                chroot > sudo servo_updater -b servo_v4

5.  **Connect the PD charger to the "DUT POWER" port of servo v4.**

6.  **Connect the type C part of the Servo V4 to your chromebook.** The DUT
    should now be charging through servo v4. Check that the green light in
    the corner of servo v4 lights up.

7.  **Verify the CCD device exists.**

    *   **Look for a device with the right vid:pid.** Cr50 vid:pid is 18d1:5014.
        You can use lsusb to check that it shows up.

                > lsusb -vd 18d1:5014

    *   **Debug connection issues**.

        *   **Port:** The DUT only supports CCD on one type C port. Try the
            other port if CCD doesn't show up.

        *   **Orientation:** Orientation shouldn't matter with Servo V4. If it
            does, please file a bug.

        *   **Charge State:** Make sure the the green light in the corner of
	    servo v4 is lit.

8.  **Connect to the Cr50 console**. After the CCD device shows up, the cr50, ec,
    and ap consoles should show up in /dev/ttyUSB\*

    *   Search for console names.

                > ls /dev/ttyUSB*

    *   If you run the `ls` command before and after connecting the Servo V4
        type C cable to the DUT, then the new devices should be the CCD
	consoles. The consoles are ordered. Cr50 should be the lowest ttyUSB
	device, then AP, and EC should have the highest number. Running `ver`
	on all of them could also let you know which one is which if you don't
	want to remember the order.
	**Servo V4 has it's own console. It might be useful to do this step to
	find the device consoles**

    *   Open the console.

                > minicom -D /dev/ttyUSB1

    *   Make sure `version` shows some version information.

9.  **Open CCD.** Here's the most generic way to open ccd. For the full open
    options see [Setup CCD].

    *   **[Enter dev mode](case_closed_debugging_cr50.md#enter-dev-mode).**
        These are clamshell instructions for other types of chromeOS devices
        refer to the full setup doc.

        *   Boot into recovery by pressing the power button, refresh, and
            escape.

        *   At the recovery screen press Ctrl+D and enter.

    *   **Use gsctool to open Cr50 from the AP. Press the power button when
        prompted.** This will take ~5 minutes.

                AP > gsctool -a -o

10.  **Set all capabilities to Always** and confirm they're Always.

                cr50 > ccd reset factory
                cr50 > ccd

11.  **Enable Testlab Mode.** Tap the power button when prompted. This will
     take a couple of seconds.

                cr50 > ccd testlab enable


12.  **Start servod and make sure the EC console works.**


    * enter chroot with `cros_sdk --no-ns-pid`

    * start servod

                chroot > sudo servod -b $BOARD

    * Check EC uart works.

                chroot > dut-control ec_board

13. **Try running a test** Use autotest_dir, so you don't need to about what
   autotest packages to emerge. firmware_ECHash is just a short test. If your
   board doesn't have an EC, try something else. Use firmware_FAFTSetup to
   verify the setup will work with faft-ec and faft-bios.

                chroot > test_that $IP --autotest_dir \
                         ~/trunk/src/third_party/autotest/files/ firmware_ECHash

14. **Debug Setup**


    *   **Cr50 capabilities:** EC uart capability needs to always be accessible.
        **Make sure UartGscTxECRx and all other capabilities are always.**

            cr50 > ccd

    *   **Make sure servod started using the CCD device**. Verify the ccd
        serialname has the right format. Cr50 serialname should have the
        format [0-9a-f]{8}-[0-9a-f]{8}

            chroot > dut-control ccd_serialname

        If the control doesn't exist or the serianame is wrong try to find
        ccd serialname and start servod explicitly selecting it.

        find serialname and use it to restart servod.

            chroot > lsusb -vd 18d1:5014 | grep iSerial
            chroot > sudo servod -b $BOARD -s $SERIALNAME


### Final Checks

 * All capabilities are Always

                cr50 > ccd

 * Testlab mode is enabled

                cr50 > ccd testlab

 * EC uart works

                chroot > dut-control  ec_board
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

[Disable SW WP]: ./case_closed_debugging_cr50.md#AP-Off
[enter dev mode]: ./case_closed_debugging_cr50.md#enter-dev-mode
[sparkfun]: https://www.sparkfun.com/products/14746
[Setup CCD]: ./case_closed_debugging_cr50.md#CCD-Setup
[SuzyQ]: https://chromium.googlesource.com/chromiumos/third_party/hdctools/+/refs/heads/master/docs/ccd.md#suzyq-suzyqable
[Type-C Servo V4]: https://chromium.googlesource.com/chromiumos/third_party/hdctools/+/refs/heads/master/docs/servo_v4.md
[wp console command]: ./case_closed_debugging_cr50.md#WP-control

