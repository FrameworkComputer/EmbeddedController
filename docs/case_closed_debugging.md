Case Closed Debugging (CCD)
===========================

Case closed debugging is a feature of the EC codebase that can bridge UART
consoles and SPI busses from a DUT (Device Under Test) to the host machine via
USB.  This allows the host to access and update much of the DUT's state.  Use
cases include automated hardware testing as well as simplified debug console
access and firmware flashing for kernel and system developers.

Prerequisites
-------------

### Supported Devices
We have added CCD support for two chip families: stm32 and gchips. Ryu,
servo\_micro, and servo\_v4 use the stm32 support. Cr50 uses gchips support.
All boards with Cr50 have support for case closed debugging.

### Suzy-Q
Suzy-Q is a Type-C dongle that exposes USB2.0/3.0 on a Type-A socket, case
closed debugging over SBU1/2 on a micro Type-B socket, and charging over a
Type-C socket.  You will need one to access the case closed debugging USB
interface.

### ChromiumOS flashrom
The functionality to flash the AP firmware image over case closed debugging is
currently only supported by the ChromiumOS version of flashrom, so you will
need to have that built.  The easiest way to do so is to just setup the
ChromiumOS SDK.

### Udev rules file
There is a udev rules file, `extra/usb_serial/51-google-serial.rules` that
should be installed in `/etc/udev/rules.d/` and udev should be told to reread
its rules.  This rules file can be installed using the
`extra/usb_serial/install` script.

### Kernel module
A trivial Linux kernel module that identifies case closed debug capable USB
interfaces can be found in `extra/usb_serial`.  This module is also built and
installed using the `extra/usb_serial/install` script.

### ChromiumOS chroot
This is really only a requirement for using flashrom.  If you only need access
to the serial consoles then a checkout of the EC firmware repository should be
sufficient.

Setup
-----

### Device Under Test (DUT)
If the DUT doesn't have a new enough PD firmware you will need to update the
firmware using a Servo, ec-tool, or MCU specific DFU mode, all of which are
outside the scope of this document.

Make sure your DUT is charged up, because while using Suzy-Q you can't currently
charge the device.

### Suzy-Q
Suzy-Q should be connected to the DUT using the Type-C cable and connector
integrated into Suzy-Q.  This connector may need to be flipped to make case
closed debugging work because the SBU lines that are used to expose the PD
MCU's USB interface are not orientation invariant.  Only one port on the DUT
will support CCD.  Try using the other port if the CCD device doesn't appear.
Suzy-Q should be connected to the Host with a Type-A (Host) to Micro Type-B
(Suzy-Q) cable. Look for the device [vendor:product ID](#Troubleshooting) to
confirm that your host sees the CCD device.

### Host
Depending on your kernel version the consoles may exist at `/dev/ttyUSB*`. If
using those works for you, you don't need to install any drivers or Udev rules.

If you want your host to generate useful symlinks for the different CCD
consoles, install the Udev rule. It will generate symlinks in
`/dev/google/<device name>/serial/<console name>` for each serial console that
a device exports.  It will also mark the DUT as incompatible with ModemManager.
This last part ensures that ModemManager doesn't attempt to open and manipulate
the various serial consoles exported by the device.

The kernel module in `extra/usb_serial` should be compiled and installed in the
running kernel.  It just adds an entry into the `usbserial` module's driver
table that causes `usbserial` to recognize any case closed debugging serial
console as a simple USB serial device.  This addition has already made its way
into the upstream kernel (v3.19), so eventually this module can be removed.
The `extra/usb_serial/install` script will build and install the kernel module
as well as install the udev rules file.

If for some reason you can't or don't want to use the kernel module the install
script provides a --fallback option that will install a udev rules file and
helper script instead that will add each new CCD capable device that is
attached to the host to the list of devices that usbserial will handle.  The
disadvantage of this method is that it will generate `/dev/ttyUSB*` entries for
any USB interface on the device that has an IN/OUT pair of bulk endpoints.
This results in extra `/dev/ttyUSB*` entries that should not be used because
they are actually I2C or SPI bridges.

The raiden module solves this by identifying a CCD serial port by the subclass
and protocol numbers of the USB device interface.  This means that there does
not need to be a list of CCD capable device IDs anywhere.

Basic CCD
---------
Here's the basic information for how to use CCD. Cr50 restricts debugging
features to try and ensure only the device owner can use CCD. For information on
how to get access to Cr50 CCD see the
[Cr50 specific CCD doc](case_closed_debugging_cr50.md).

### Consoles
The serial consoles exposed by case closed debugging can be found in
`/dev/ttyUSB*` or `/dev/google/<device name>/serial/<console name>` if you
installed the Udev rules. The consoles can be opened with any program that you
would normally use to open a TTY character device such as minicom or screen.

If you installed the Udev rules, the console path will be determined based on
the device and USB bus. The `<device name>` field is generated from the DUT's
USB descriptor `iProduct` field as well as the USB bus index and device path on
that bus (the list of port numbers for the hub connections between the Host and
DUT).  As such it is unique to a particular setup and won't change across
reboots of either the Host or the DUT.  The `<console name>` field is just the
`iInterface` USB descriptor field from the particular USB interface that
is associated with this console device. This allows a single DUT to expose
multiple serial consoles in a discoverable and consistent manner.

If you're using the consoles at `/dev/ttyUSB*`, you can just check which console
it is by running a few commands like `version`.

### Flash AP
Programming the AP SPI flash with a new firmware image can be done with flashrom
using the command:

`sudo /usr/sbin/flashrom -p raiden_debug_spi -w /build/<board>/firmware/image.bin`

If there are more than one case closed debug capable devices connected to the
host you will have to disambiguate the DUT with additional programmer parameters.
Flashrom will list all DUTs that are found along with programmer parameters that
can be used to identify the intended DUT.  Flashrom programmer parameters are
added to the programmer name (the -p argument) by appending a colon and then a
comma separated list of key=value pairs. The `serial` parameter is best for this

`sudo /usr/sbin/flashrom -p raiden_debug_spi:serial=${SERIAL} -w
/build/<board>/firmware/image.bin`

Cr50 can be used to flash the AP or EC.  You will need to specify the AP as the
target device, so cr50 knows to flash the AP.
`sudo flashrom -p raiden_debug_spi:target=AP -w image.bin`

### Flash EC
You can use `util/flash_ec` to flash the EC.  Steps for flashing the EC are more
complex and board specific than flashing the AP.  This script will handle all
the board specific setup.

Known Issues
------------

1. Charge and the use of the Type-A port on Suzy-Q do not work, so for now if
you need to attach a flash drive, or use Fastboot/adb you'll need to swap
cables.

2. Ryu implementation: software sync of the EC/PD processor and the jump from
RO to RW versions will cause the case closed debugging USB device to disconnect
and reconnect. This can be prevented by disabling software sync.  This is done
by setting the `GBB_FLAG_DISABLE_EC_SOFTWARE_SYNC` and `
GBB_FLAG_DISABLE_PD_SOFTWARE_SYNC` flags with`gbb_utility.

Troubleshooting
---------------
Check for the CCD device using the following vendor:product IDs

| Device | VID:PID |
| :---| :---: |
| servo_micro | 18d1:501a |
| servo_v4 | 18d1:501b |
| ryu | 18d1:500f |
| cr50 | 18d1:5014 |

1. Can't see the CCD device on the host.

	1. Type-C cable from Suzy-Q to the DUT may be upside down.  The SBU lines
	used for case closed debugging are not orientation invariant.
	2. You may be using the wrong device port. Try using the other port.
	3. The device may not be charged enough to boot. Suzy-Q can't charge the
	device or supply enough power for the DUT to boot. Make sure the device
	is somewhat charged.

2. No console interfaces are available in the `/dev/google/<name>` directory.

	1. Kernel module may not be loaded.
	2. Udev rules file might not be installed correctly.
	3. PD firmware version may be too old.
	4. Type-C cable from Suzy-Q to the DUT may be upside down.  The SBU lines
	used for case closed debugging are not orientation invariant.

3. Garbage messages (AT command set) show up on one or more consoles.

	1. ModemManager has claimed the interface, Udev rules file may not be
	installed correctly.

4. Console interfaces appear and then quickly disappear

	1. Software sync from the AP has replaced the PD firmware with a version
	that is not compatible with case closed debugging.
