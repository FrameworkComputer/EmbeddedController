Case Closed Debugging
=====================

Case closed debugging is a feature of the EC codebase that can bridge UART
consoles and SPI busses from a DUT (Device Under Test) to the host machine via
USB.  This allows the host to access and update much of the DUT's state.  Use
cases include automated hardware testing as well as simplified debug console
access and firmware flashing for kernel and system developers.

Prerequisites
-------------

### Ryu
Currently only Ryu has support for case closed debugging.  The first version of
Ryu that supported case closed debugging was P3.

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
MCU's USB interface are not orientation invariant.  Suzy-Q should be connected
to the Host with a Type-A (Host) to Micro Type-B (Suzy-Q) cable.

### Host
The Udev rule file should be installed, it will generate useful symlinks in
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

Use
---

The serial consoles exposed by case closed debugging can be found in
`/dev/google/<device name>/serial/<console name>` and can be opened with any
program that you would normally use to open a TTY character device such as
minicom or screen.  The `<device name>` field is generated from the DUT's USB
descriptor `iProduct` field as well as the USB bus index and device path on
that bus (the list of port numbers for the hub connections between the Host
and DUT).  As such it is unique to a particular setup and won't change across
reboots of either the Host or the DUT.  The `<console name>` field is just the
`iInterface` USB descriptor field from the particular USB interface that is
associated with this console device.  This allows a single DUT to expose
multiple serial consoles in a discoverable and consistent manner.

Programming the AP SPI flash with a new firmware image can be done with flashrom
using the command:

`sudo /usr/sbin/flashrom -p raiden_debug_spi -w /build/<board>/firmware/image.bin`

If there are more than one case closed debug capable devices connected to the
host you will have to disambiguate the DUT with additional programmer parameters.
Flashrom will list all DUTs that are found along with programmer parameters that
can be used to identify the intended DUT.  Flashrom programmer parameters are
added to the programmer name (the -p argument) by appending a colon and then a
comma separated list of key=value pairs.

Known Issues
------------

1. Charge and the use of the Type-A port on Suzy-Q do not work, so for now if
you need to attach a flash drive, or use Fastboot/adb you'll need to swap
cables.

2. Software sync of the EC/PD processor and the jump from RO to RW versions
will cause the case closed debugging USB device to disconnect and reconnect.
This can be prevented by disabling software sync.  This is done by setting the
`GBB_FLAG_DISABLE_EC_SOFTWARE_SYNC` and `GBB_FLAG_DISABLE_PD_SOFTWARE_SYNC` flags
with `gbb_utility`.

Troubleshooting
---------------

1. No console interfaces are avaiable in the `/dev/google/<name>` directory.

	1. Kernel module may not be loaded.
	2. Udev rules file might not be installed correctly.
	3. PD firmware version may be too old.
	4. Type-C cable from Suzy-Q to the DUT may be upside down.  The SBU lines
	used for case closed debugging are not orientation invariant.

2. Garbage messages (AT command set) show up on one or more consoles.

	1. ModemManager has claimed the interface, Udev rules file may not be
	installed correctly.

3. Console interfaces appear and then quickly disappear

	1. Software sync from the AP has replaced the PD firmware with a version
	that is not compatible with case closed debugging.
