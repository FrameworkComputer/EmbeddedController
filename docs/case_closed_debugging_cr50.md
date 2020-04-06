# Google Security Chip (GSC) Case Closed Debugging (CCD)

Cr50 is the firmware that runs on the Google Security Chip. It has support for
Case Closed Debugging (CCD). This support is complete enough to replace servo.
This doc explains how to setup CCD, so you can access all of the necessary
features to develop firmware on your device or control different components.
Please run through the basic setup steps before trying to flash the AP firmware.
CCD could help recover your device if you flash broken firmware, but **if you
brick your device before setting up CCD, you may not be able to enable it**.

[TOC]

# Background

Cr50 CCD was designed to restrict CCD access to device owners. There are **CCD
privilege levels** that can be used to enable access to different CCD
capabilities **Open, Unlocked, Locked**.

All CCD functionality has been assigned to different **CCD capabilities**.
Capability settings can be modified to require certain privilege levels to
access the each capability. Setting a capability requirement to **IfOpened**
will require a level of **Open** to access that capability. A requirement of
**UnlessLocked** will require the device to be **Open** or **Unlocked** to
access the capability. Setting the requirement to **Always** will make the
capability always accessible.

Owners can use these settings to customize the CCD so it is as open or
restricted as they want.

# CCD Capabilities

Cr50 is locked by default. Here are all of the Capabilities and their default
settings.

Capability      | Default  | Function
--------------- | -------- | --------
UartGscRxAPTx   | Always   | controls reading from the AP console
UartGscTxAPRx   | Always   | controls writing to the AP console
UartGscRxECTx   | Always   | controls reading from the EC console
UartGscTxECRx   | IfOpened | controls writing to the EC console
FlashAP         | IfOpened | controls flashing the AP
FlashEC         | IfOpened | controls flashing the EC
OverrideWP      | IfOpened | controls controlling write protect
RebootECAP      | IfOpened | controls rebooting the EC/AP from the cr50 console
GscFullConsole  | IfOpened | controls access to restricted Cr50 console commands
UnlockNoReboot  | Always   | controls unlocking Cr50 without rebooting the AP
UnlockNoShortPP | Always   | controls unlocking Cr50 without physical presence
OpenNoTPMWipe   | IfOpened | controls opening Cr50 without wiping the TPM
OpenNoLongPP    | IfOpened | controls opening Cr50 without physical presence
BatteryBypassPP | Always   | controls opening cr50 without physical presence and dev mode if the battery is removed
UpdateNoTPMWipe | Always   | controls updating cr50 without wiping the TPM
I2C             | IfOpened | controls access to the I2C master (used for measuring power)
FlashRead       | Always   | controls dumping a hash of the AP or EC flash
OpenNoDevMode   | IfOpened | controls opening cr50 without dev mode
OpenFromUSB     | IfOpened | controls opening cr50 from USB

# CCD Setup

Some basic CCD functionality is accessible by default. There is read-only access
to the EC console and read-write access to the AP console. There are some basic
cr50 console commands you can run.

Cr50 CCD needs to be opened to access all CCD functionality or to modify
capability settings so the device doesn't need to be open to access CCD
functionality.

## Prerequisites

Cr50 needs to be newer than 0.3.9 or 0.4.9 to setup ccd. The 3 in the major
version means it's a MP image and 0.4.X is a prePVT image. There aren't many
differences between the MP and prePVT versions of images. It is just a little
easier to CCD open prePVT images. You can't run prePVT images on MP devices, so
if you're trying to update to .prepvt and it fails try using .prod.

*   Sync chroot to TOT (run repo sync in chromiumos directory) update servod and
    gsctool in chroot

        from chroot > sudo emerge hdctools ec-devutils servo-firmware
                  chromeos-cr50 chromeos-cr50-scripts

*   Update servo v4 firmware

        from chroot > sudo servo_updater -b servo_v4

*   Ensure cr50 firmware is up to date. You can run these gsctool commands from
    the AP console or you can run them as root from inside the chroot if suzyq
    is connected.

    *   If you're doing this from the AP, install a test image newer than M66.
    *   check the cr50 version

                sudo gsctool -a -f

    *   If the RW version is greater than 0.(3|4).9 then you don't need to
        update cr50. If it's not, then you need to update cr50.

    *   Update cr50.

                sudo gsctool -a /opt/google/cr50/firmware/cr50.bin.prod

    *   Check the cr50 version again to make sure it's now newer than 0.X.9

*   Ensure power isolation on servo v4

    *   Plug USB-C power into servo v4 for dut pass though
    *   Green LED will light up when plugged into DUT.

## Basic Steps for CCD setup

1.  Use the general [setup](case_closed_debugging.md#Setup) instructions to
    connect Suzy-Q and access the Cr50 console. The Cr50 console will be the
    lowest `/dev/ttyUSB*` device created by Cr50 or `/dev/google/<device
    name>/serial/Shell`

2.  [Open CCD](#Open-CCD)

3.  [Modify capabilities](#Enable-Open-Without-Requiring-Device-to-Boot) to make
    it easier to open cr50 or access necessary capabilities - this step is
    optional, but **the open state will be lost if cr50 reboots or the device
    loses power**. If your use of CCD will possibly brick the device, it is
    recommended you modify the capability settings or set a ccd password, so you
    can reopen the device.

4.  Use CCD

## Open CCD

The first cr50 image with CCD support was 0.3.9. If you are not running 0.3.9,
you need to download the image and update cr50 from the AP or using Suzy-Q.
https://storage.googleapis.com/chromeos-localmirror/distfiles/cr50.r0.0.10.w0.3.9.tbz2

You can download the cr50 image and then flash cr50 using Suzy-Q from the chroot

    from chroot > sudo gsctool cr50.r0.0.10.w0.3.9/cr50.bin.prod

If you are only briefly using ccd and aren’t doing anything that may brick the
device, you can probably just stick to opening cr50. **The open state will be
lost after cr50 reboot. If you don’t want to have to reopen cr50, you may want
to setup the ccd capabilities so you can use them without needing cr50 to be
open.**

### Standard Process (Requires Booting to Kernel)

If your device can boot, you can open Cr50 by entering dev mode and then sending
the ccd open command from the kernel.

#### Enter dev mode

Entering dev mode has to be done manually. Using the gbb flags to force dev mode
will not work.

1.  First, on a root shell on the device, check the force dev mode flag isn’t
    set GBB flags by running. If you can’t access the shell, because you aren’t
    in dev mode, then you’re fine. You can skip steps 1, 2, and 3.

        AP > /usr/share/vboot/bin/get_gbb_flags.sh

2.  Clear 0x8 from the GBB flags and set the new value

        AP > /usr/share/vboot/bin/set_gbb_flags.sh $OLD_FLAG_VALUE & ~0x8

3.  Reboot the device

4.  Put the device into recovery mode

    -   **Tablets/Detachables** - hold power button vol up and vol down for 10
        seconds. Release and wait until the device boots into recovery
    -   **Clamshells/Convertibles** - press power button escape refresh
    -   **Chromeboxes** - Use a paperclip to press the recovery button while
        plugging in AC.
    -   **Using servo** - If cr50 is open or you are using a flex cable you can,
        you can use `dut-control power_state:rec`

5.  Enable developer mode

    -   **Tablets/Detachables** - After the device boots into recovery, press
        volume up and volume down at the same time to get to the enable dev mode
        menu. Use volume up button to navigate to “confirm disabling os
        verification” use the power button to select it
    -   **Clamshells/Convertibles** - press ctrl+d on keyboard or AP console to
        select developer mode then enter to enable it.
    -   **Chromeboxes** - Use a paper clip to press the dedicated recovery
        button

6.  Verify Cr50 knows the device is in dev mode. The TPM state will print
    `dev_mode` if cr50 knows the device is in dev mode. If it doesn’t say
    `dev_mode`, ccd open will fail. If you see "`TPM: dev_mode`" you are okay to
    CCD open now. **If you don’t see `TPM: dev_mode`, recheck the gbb flags to
    make sure they aren’t forcing dev mode. Retry the manual entry of dev
    mode.**

```
    cr50 > ccd
          State: Locked
          Password: none
          Flags: 0x000001
          Capabilities: 0000000000000000
          UartGscRxAPTx   Y 0=Default (Always)
          UartGscTxAPRx   Y 0=Default (Always)
          UartGscRxECTx   Y 0=Default (Always)
          UartGscTxECRx   - 0=Default (IfOpened)
          FlashAP         - 0=Default (IfOpened)
          FlashEC         - 0=Default (IfOpened)
          OverrideWP      - 0=Default (IfOpened)
          RebootECAP      - 0=Default (IfOpened)
          GscFullConsole  - 0=Default (IfOpened)
          UnlockNoReboot  Y 0=Default (Always)
          UnlockNoShortPP Y 0=Default (Always)
          OpenNoTPMWipe   - 0=Default (IfOpened)
          OpenNoLongPP    - 0=Default (IfOpened)
          BatteryBypassPP Y 0=Default (Always)
          UpdateNoTPMWipe Y 0=Default (Always)
          I2C             - 0=Default (IfOpened)
          FlashRead       Y 0=Default (Always)
          OpenNoDevMode   - 0=Default (IfOpened)
          OpenFromUSB     - 0=Default (IfOpened)
          TPM: dev_mode                         <==== This is the important part
          Use 'ccd help' to print subcommands
```

#### Run ccd open

You can start the open process from the AP. Once you start the process, you will
need to press the power button when prompted open cr50.

1.  Start the ccd open process from the AP.

         AP shell > gsctool -a -o

2.  Over the next 5 minutes you will be prompted to tap the power button.

3.  After the process is finished, use ‘ccd’ on the cr50 console to verify the
    state is open

The Open setting will be lost whenever cr50 reboots. Make sure to setup ccd so
you will be able to recover the device even if Open is lost. To open cr50 you
need access to the AP. If your debugging will make the AP inaccessible and you
want to ensure that you can recover the device, you either need to modify the
capability settings so you can access the capabilities necessary to recover the
device while cr50 is locked or you need to modify the capabilities so you don't
need the AP to open cr50.

If you need to reflash the AP or EC, you can set the FlashEC or FlashAP
capabilities to Always.

If you want to be able to open cr50 without the AP, set OpenNoDevMode and
OpenFromUSB to Always.

### CCD Open Without Booting the Device

If you can’t boot the device, you won’t be able to enter dev mode and send the
open command from the AP. You will need to follow some non-standard methods to
open the device. If you have enabled ccd before, cr50 may be configured in a way
that you can still open cr50. If you haven't setup CCD before, you will need to
remove the battery to enable CCD.

#### Can remove the Battery

If you can remove the battery, you can bypass the AP command/dev mode
requirements. `ccd open` is allowed from the console if FWMP doesn’t disable ccd
and the battery is disconnected. This is the most universal method and will work
even if you haven’t enabled ccd before. Some devices are glued shut if you
can’t/don’t want to unglue your device do not rely on this method. Setup ccd
correctly before flashing the AP/EC.

1.  Disconnect the battery

2.  Send ‘ccd open’ from the cr50 console.

#### CCD testlab is enabled

You can check if testlab is enabled cr50 from the console.

    cr50 > ccd testlab
            CCD test lab mode enabled

If it’s enabled, you can open cr50 from the console without physical presence.

    cr50 > ccd testlab open

#### OpenNoDevMode and OpenFromUSB are set to Always

This requires >=0.3.10. If these capabilities are set, you will be able to open
cr50 from the console without dev mode.

    cr50 > ccd open

#### CCD Password is Set

You can run ccd open with the password to open from the console.

    cr50 > ccd open $PASSWORD

## Configure CCD

Cr50 capabilities allow you to configure CCD to restrict or open the device as
much as you want. You can use the `ccd` command to check and modify the
capabilities. Cr50 has to be open to change the capabilities. Setting
capabilities you want to use to Always will make them accessible even if cr50
loses the open state. If you are using capabilities that may cause cr50 to
reboot or may brick the device, you should set the capabilities needed to
recover the device to Always or setup the capabilities so you can open cr50
without booting the device.

Basic ccd functionality is covered by UartGscTxECRx, UartGscRxECTx,
UartGscTxAPRx, UartGscRxAPTx, FlashAP, FlashEC, OverrideWP, and GscFullConsole.

You can go through the capability descriptions and figure out which ccd
capabilities you want to use. After you figure that out you can modify the
capabilities to Always be accessible.

    > ccd set $CAP $REQ

For example if the EC console needs to be read-write even when Cr50 is locked
set the capability to Always.

    > ccd set UartGscTxECRx Always

If you want to restrict capabilities more you can set them to IfOpened. If you
don’t want the AP/EC uart to be accessible at all when cr50 is locked, you can
set them all to IfOpened.

Restrict EC

    > ccd set UartGscTxECRx IfOpened
    > ccd set UartGscRxECTx IfOpened

Restrict AP

    > ccd set UartGscTxAPRx IfOpened
    > ccd set UartGscRxAPTx IfOpened

If you want things as accessible as possible and want all capabilities to be
Always, you can run

    > ccd reset factory

This will also permanently disable write protect. To reset write protect run

    > wp follow_batt_pres atboot

To reset capabilities to Default run

    > ccd reset

### Enable Open Without Requiring Device to Boot

By default Cr50 requires enabling dev mode before you can open the device and
the open command has to be sent from the AP. You can change the capabilities to
remove these requirements if you think your development may prevent the device
from booting. You can also set the ccd password to get around these
requirements. These options offer different pros and cons. You can decide which
is best for you.

#### Set Capabilities

After opening cr50, you can set these capabilities to reduce the restrictions
required to open cr50.

    > ccd set OpenFromUSB Always
    > ccd set OpenNoDevMode Always

#### CCD Password

A ccd password can also be used to get around the open restrictions. The
password will be required to reopen or unlock cr50, so keep track of the
password. It can't be reset unless cr50 is open, so if you forget it, nothing
can be done to reopen cr50.

##### Set Password

Run the gsctool command and enter the password when prompted. It will prompt for
the password twice.

    from ap shell > gsctool -a -P

You can use the ccd command to check if the password is set.

    cr50 > ccd
           ...
           Password: [none|set]
           ...

##### Clear Password

You can clear the password by opening cr50 and then running the gsctool command
agatin. When prompted for the password enter `clear:$PASSWORD` at both prompts.

You can also use the cr50 `ccd reset` command when cr50 is open. This will clear
the password and reset all ccd capabilities to default.

##### Use Password

After the password has been set you can use it to run ccd commands from the cr50
console.

    cr50 > ccd open $PASSWORD
    cr50 > ccd unlock $PASSWORD

You can use it from the AP shell

    cr50 > gsctool -a -o
    cr50 > gsctool -a -u

enter the password when prompted

# Using CCD

## Rddkeepalive

Cr50 only enables ccd when it detects a debug accessory is connected. It detects
the cable based on the voltages on the CC lines. If you are flashing the EC and
AP, these cc voltages may become unreliable for detecting a debug accessory. You
can use a cr50 command to tell cr50 to ignore the voltages on these cc lines and
just keep ccd enabled. There are many things that could interfere with rdd, so
it’s probably best to run this before doing anything else using ccd.

    cr50 > rddkeepalive enable

This command is useful for making sure ccd stays enabled during debugging. It
will increase cr50 power a lot when the debug cable is disconnected. If you’re
worried about that, disable rddkeepalive when you're not using ccd.

    cr50 > rddkeepalive disable

## Consoles

Cr50 presents 3 consoles through CCD. It has the AP, EC, and Cr50 console. The
AP and EC consoles can be restricted using the 4 ccd uart capabilities
**UartGscRxAPTx, UartGscTxAPRx, UartGscRxECTx, UartGscTxECRx**. The default
setup is the AP is read write. The EC is read only. Cr50 console input/output
can’t be suppressed.  You can only use the **GscFullConsole** capability to
enable restricted console commands.

Cr50 will create 3 /dev/ttyUSBX devices. They’re the cr50, AP, and EC console.
The cr50 console normally has the lowest number. The AP and EC are the other
two. You can figure out which one is which by pressing the power button or
rebooting the device and looking at the uart output. If you have a bunch of
devices, you can unplug suzyq and see which devices disappear to figure out the
relevant ones.

Servo can also figure this out for you. You can start servo like you normally do

    chroot > sudo servod -b $BOARD

After starting servo, you can use dut-control to get the consoles

    chroot > dut-control cr50_uart_pty ec_uart_pty cpu_uart_pty

## Flashing EC

This is restricted by the **FlashEC** capability. This must be accessible to
flash the EC.

The steps to flash the EC differ a lot based on what board you’re using. You
should stick to using `flash_ec` to handle flashing the ec, because the steps
can get pretty complicated and are board specific.

You will need to start servod then `flash_ec` will handle sending the correct
cr50 console commands and updating the EC.

    chroot > sudo servod -b $BOARD
    chroot > ~/trunk/src/platform/ec/util/flash_ec -i $IMAGE -b $BOARD

## Flashing the AP

This is restricted by the **FlashAP** capability. This must be accessible to
flash the AP.

Flashing the AP is standard across boards.

    chroot > sudo flashrom -p raiden_debug_spi:target=AP -w $IMAGE

This default flashing command takes a very long time to complete, there are ways
to speed up flashing process by cutting some corners, see the next section.

If you have a lot of ccd devices plugged in, you may want to use the cr50
serialname. You can get this by running

    chroot > lsusb -vd 18d1:5014 | grep iSer

You can add the serialname to the flashrom command using

    chroot > sudo flashrom -p raiden_debug_spi:target=AP,serial=$SERIAL -w $IMAGE

**If you don’t see cr50 print any messages when you’re running the flashrom
command, you probably need to use the serialname.**

### Speeding up flashing the AP

The flashrom utility is designed without any consideration of the speed of
access to the chip. While this is a reasonable omission when talking about
inline programming, when the AP controls the SPI bus and can clock the bus at 50
MHz, speed of access becomes a very annoying limitation when programming AP
flash using Cr50, where SPI bus can not be clocked faster than 1.5 MHz.

One of 'smart' tricks deployed by flashrom is reading the entire flash contents
first, before programming, then erase and or program only flash pages which have
to be modified. Some pages are probably still the same since the previous image
was written, some pages could be programmed without erasing, etc.

This trick is especially painful when flashing using Cr50, because reading the
entire flash image clocking it at 1.5MHz alone takes a few minutes.

After programming is completed, by default flashrom reads the flash contents
back and compares them with the file used for programming. This second read also
takes a few minutes, even if small portions of the chip were modified, the
entire chip is read and compared.

With these shortcomings in mind, a more efficient way of programming AP flash
can be proposed. The flashrom utility was enhanced to prevent it from reading
the contents of the flash before programming, and from reading the entire chip
when verifying the write operation. It turns out it takes much less time to
erase the entire flash chip first and then program only sections essential for
the device booting in recovery mode. Once the device boots in recovery mode
Chrome OS can be installed from a removable storage device, and then the entire
flash can be programmed from the Chrome OS command line, with the AP clocking
the bus at 50 MHz.

Incidentally, any Chrome OS device AP firmware image is split into sections,
which can be examined by running the command `dump_fmap <image file>`

The `dump_fmap` utility finds in the image the section called `FMAP` and prints
out its contents, which describes the layout of the entire flash image.

Only a few sections are essential for maintaining the device identity and for
booting the device in recovery mode. The `-i` command line option of `flashrom`
allows the user to indicate which sections should be read/written.

The below sequence of commands allows to quickly reprogram the AP flash: ``` #
This will save device flash map and VPD sections in # /tmp/bios.essentials.bin.
VPD sections contain information like device # firmware ID, WiFi calibration,
enrollment status, etc. Use the below command # only if you need to preserve the
DUT's identity, no need to run it in case # the DUT flash is not programmed at
all, or you do not care about preserving # the device identity. sudo flashrom -p
raiden_debug_spi:target=AP -i FMAP -i RO_VPD -i RW_VPD -r
/tmp/bios.essentials.bin --fast-verify

\# This command will erase the entire flash chip in one shot, the fastest #
possible way to erase. sudo flashrom -p raiden_debug_spi:target=AP -E
--do-not-diff

\# This command will program essential flash sections necessary for the # Chrome
OS device to boot in recovery mode. Note that the SI_ALL section is # not always
present in the flash image, do not include it if it is not in # dump_fmap
output. sudo flashrom -p raiden_debug_spi:target=AP -w image-atlas.bin -i FMAP
-i WP_RO [-i SI_ALL] --do-not-diff --noverify

\# This command will restore the previously preserved VPD sections of the #
flash, provided it was saved in the first step above. sudo flashrom -p
raiden_debug_spi:target=AP -w /tmp/bios.essential.bin -i RO_VPD -i RW_VPD
--do-not-diff --noverify ```

Once flash is programmed, the device can be booted in recovery mode and start
Chrome OS from external storage, following the usual recovery procedure. Once
Chrome OS is installed, AP flash can be updated to include the rest of the image
by running flashrom or futility from the device bash prompt.

## WP control

This is restricted by the **OverrideWP** capability. If this capability is
accessible, you can use the cr50 `wp` command. If it's not, you can only control
write protect using battery presence.

### WP console command

You can use the cr50 console command to change the write protect settings.

There are three write protect settings: `forced enabled, forced disabled,
follow_batt_pres`.

*   **`follow_batt_pres`** - DEFAULT SETTING - use battery presence to determine
    the write protect setting. If the battery is connected, enable write
    protect. If the battery is disconnected, disable write protect. If the board
    doesn’t have a battery, then normally a screw is used. If the screw is
    present, enable wp. If it’s not, disable wp.

*   **`enabled`** - enable write protect no matter the state of the battery.
    Protect things like the AP/EC flash and various other components that use
    this write protect signal

*   **`disabled`** - write protect is deasserted no matter the state of the
    battery. You’ll be able to modify things like AP RO

You can set these from the cr50 console

    cr50 > wp [enable|disable|follow_batt_pres]

This setting will persist until it is cleared using the wp command or until cr50
reboots/loses power. After these resets, cr50 will default to the atboot
setting. The default setting is follow\_batt\_pres, so cr50 will go back to
following battery presence after reboot unless the atboot setting has been
overridden.

Using the `atboot` arg will update the current and atboot wp state. If the
`atboot` arg is given to the wp command, then the setting will persist until it
is cleared by the wp command. It won’t be reset by anything else, so if you only
want to disable/enable write protect for a short time, make sure atboot is set
to `follow_batt_pres`. If you want to permanently disable or enable write
protect and want to ignore the battery, this is a good setting to update.

    cr50 > wp [enable|disable|follow_batt_pres]

You can use the wp command to get the write protect state even if the capability
is restricted.

```
    cr50 > wp

           Flash WP: [forced ]enabled|disabled
             atboot: forced enabled | force disabled | follow_batt_pres
```

Gsctool also supports getting the write protect state

    AP > gsctool -a -W

The output will show the current and atboot setting.

The current wp setting will not explicitly show that write protect is currently
following battery presence. You have to get this by checking if the wp state is
‘forced’ enabled/disabled. Forced means write protect is being overridden by the
console command. If it just shows the state without forced, write protect is
following battery presence.

The atboot setting shows what the wp state will reset to after reboot.

### Battery Presence

If the OverriedWP command isn’t accessible, you can use battery presence to
change the wp state as long as the wp setting is still `follow_batt_pres`.

*   wp disable - disconnect the battery

*   wp enable - connect the battery

If the wp setting has been overridden by ccd, this won’t work until the current
wp setting is reset to `follow_batt_pres`

    cr50 > wp follow_batt_pres atboot

### HW WP Issues

#### Chromeboxes

Chromeboxes do not have batteries, so cr50 can't use battery presence for write
protect. They use a write protect screw. You need to remove the write protect
screw to disable write protect if cr50 is set to `follow_batt_pres`.

#### Bob

Bob's have a write protect screw in addition to battery presence. The write
protect screw will force enable write protect until it's removed. If cr50 is set
to `follow_batt_pres`, you need to remove the write protect screw and disconnect
the battery to disable write protect. If you run `wp disable`, you will also
need to remove the screw.

#### AP Off

Cr50 puts the device in reset to flash the AP. Due to hardware limitiations Cr50
may not be able to disable write protect while the device is in reset. If you
want to reflash RO firmware using CCD and your board has issues disabling HW WP,
you may need to disable SW write protect.

Check if your board has this issue

1.  Disable write protect using the cr50 console command

2.  Check it's still disabled when the AP is off. This command should show write
    protect is disabled. If it shows it's enabled, then cr50 can't disable WP
    when the AP is off. You should disable SW WP to flash RO firmware using ccd.

        chroot > sudo flashrom -p raiden_debug_spi:target=AP --wp-status

Disable SW WP if the ccd flashrom command doesn't show write protect disabled.

    from AP > flashrom -p host --wp-disable

# CCD as a Servo replacement

Once cr50 is open and all capabilities have been set to Always, cr50 should be
able to be used as a servo replacement. It has all of the capabilities servo
does and support has been added to hdctools to convert servo controls to cr50
and ec console commands.

If you start servod and select the ccd device, you should be able to use servo
dut-control comands normally.

    chroot > sudo servod -b $BOARD

If cr50 reboots or usb disconnects for some reason, servod will lose the
connection to the cr50 usb. Support has just been added to hdctools to
reinitialize all of the servo ccd interfaces, so things should come back up
after the disconnect, but it might still have bugs.

Servo can take care of a lot of the less intuitive things for you like during
init it will send `rddkeepalive enable`. It will also find the AP, EC, and Cr50
uart. Servod knows how to interact with the i2c endpoint, so you can use servod
to read power from the INAs if they’re populated.

Suzyq doesn’t have all of the necessary things to replace servo for FAFT, but
you should be able to use it for normal debugging functionality. You will need a
type c servo v4 for ccd if you need to run FAFT.

# UART Rescue mode

## Overview

UART Rescue Mode is a feature of the Cr50 RO firmware that supports programming
the RW firmware using only the UART interface. This is used to recover a bad RW
firmware update (which should be rare).

This is also useful when bringing up new designs, as this allows to update Cr50
image even before USB CCD or TPM interfaces are operational.

UART rescue works on all existing devices, all it requires is that Cr50 console
is mapped to a `/dev/xxx` device on the workstation (the same device used to
attach a terminal to the console).

Rescue works as follows: when the RO starts, it prints out on the console a
certain string and momentarily waits for the host to send a sync symbol, to
indicate that an alternative RW will have to be loaded over UART. The RO also
enters this mode if there is no valid RW to run.

When rescue mode is triggered, the RO is expecting the host to transfer a single
RW image in hex format.

## Install the cr50-rescue utility

The `cr50-rescue` utility is used to flash a given firmware to cr50 using rescue
mode. This tool must be installed inside the chroot.

```bash
sudo emerge cr50-utils
```

## Preparing an RW image

To prepare the signed hex RW image, fetch a released image from Google storage,
which can be found by running:

`gsutil ls gs://chromeos-localmirror/distfiles/cr50*`

(depending on your setup you might have to do this inside chroot). Copy the
image you want to use for rescue to your workstation and extract cr50.bin.prod
from the tarball.

The latest cr50 images can be found in the [chromeos-cr50 ebuild]. Generally,
you should always use the PROD_IMAGE indicated in that file. Once rescued, the
user can update to the PREPVT image later if needed.

Once the binary image is ready, use the following commands to carve out the RW A
section out of it and convert it into hex format:

```bash
dd if=<cr50 bin file> of=cr50.rw.bin skip=16384 count=233472 bs=1
objcopy -I binary -O ihex --change-addresses 0x44000 cr50.rw.bin cr50.rw.hex
```

then you can use `cr50.rw.hex` as the image passed to `cr50-rescue`.

## Programming the RW image with rescue mode

With servo_micro (or servo_v2 reworked for connecting to Cr50 console), run
servod and disable cr50 ec3po and uart timestamp: `bash dut-control
cr50_uart_timestamp:off dut-control cr50_ec3po_interp_connect:off`

Get a raw cr50 uart device path and use it for cr50-rescue argument ‘-d’ below.
`bash dut-control raw_cr50_uart_pty`

Prior to running `cr50-rescue`, the terminal from the cr50 console UART must be
disconnected, and cr50 must be unpowered-- the system needs to have AC power and
battery disconnected.

After ensuring those steps, the rescue command may be run as follows:

```bash
cr50-rescue -v -i <path to the signed hex RW image>
        -d <cr50 console UART tty>
```

After starting the command, provide power to the board and rescue mode will
start automatically. After flashing successfully (see sample output below), cr50
must be unpowered again, by disconnecting AC power and battery.

Note that `<cr50 console UART tty>` above has to be a direct FTDI interface,
`pty` devices created by servod do not work for this purpose. Use either
servo-micro or a USB/UART cable. Note that multifunctional *SPI-UART/FTDI/USB
cables might not work*, as they impose a significant delay in the UART stream,
which makes the synchronization described below impossible.

`cr50-rescue` starts listening on the console UART and printing it out to the
terminal. When the target is reset, `cr50-rescue` detects the `Bldr |` string in
the target output, at this point the utility intercepts the boot process and the
target proceeds to receiving the new RW image and saving it into flash. Note the
currently present RW and RW_B images will be wiped out first.

### Sample output

```
cr50-rescue -v -i cr50.3.24.rw.hex -d /dev/pts/0
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

[chromeos-cr50 ebuild]: https://chromium.googlesource.com/chromiumos/overlays/chromiumos-overlay/+/refs/heads/master/chromeos-base/chromeos-cr50/chromeos-cr50-0.0.1.ebuild
