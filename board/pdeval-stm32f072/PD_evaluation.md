USB PD chip evaluation configuration
====================================

This board configuration implements a USB Power Delivery TCPM
in order to evaluate various TCPC chips.
The code tries to follow the preliminary USB PD interface standard but for TCPC chip implementing proprietary I2C protocol, a new TCPM file can be implemented as explained in the [Updating the code](#Updating-the-code) section below.

Building
--------

### ChromiumOS chroot

All the following instructions have been verified in a ChromiumOS chroot.
You can find how to set one up on the Chromium development wiki:
[http://dev.chromium.org/chromium-os/quick-start-guide](http://dev.chromium.org/chromium-os/quick-start-guide)

### Build the TCPM code

`cd src/platform/ec`

`make BOARD=pdeval-stm32f072`


Updating the code
-----------------

### TCPC Communication code

Please duplicate [driver/tcpm/tcpci.c](../../driver/tcpm/tcpci.c) into **driver/tcpm/<vendor>.c**.
Then update the control logic through I2C there.

### Board configuration

In [board/pdeval-stm32f072/board.h](board.h), you can update `CONFIG_USB_PD_PORT_COUNT` to the actual number of ports on your board.
You also need to create/delete the corresponding `PD_Cx` tasks in [board/pdeval-stm32f072/ec.tasklist](ec.tasklist).

By default, the firmware is using I2C1 with SCL/SDA on pins PB6 and PB7, running with a 100kHz clock.
To change the pins or speed, you need to edit `i2c_ports` in [board/pdeval-stm32f072/board.c](board.c), update `I2C_PORT_TCPC` in [board/pdeval-stm32f072/board.h](board.h) with the right controller number, and change the pin mux in [board/pdeval-stm32f072/gpio.inc](gpio.inc).

An interrupt line, PA1, is configured to be used for the TCPC to get the attention of the TCPM. The GPIO is configured to trigger an interrupt on the falling edge and will call `tcpc_alert()`, which must be implemented in **driver/tcpm/<vendor>.c**, and should determine the cause of the interrupt and take action. The GPIO can be changed in [board/pdeval-stm32f072/gpio.inc](gpio.inc).

Flashing and Running
--------------------

### Flashing the firmware binary

To flash through JTAG with OpenOCD, you can just run:

`sudo make flash BOARD=pdeval-stm32f072`

Note: you need to do that with your USB mini-B cable is connected to the **USB ST-LINK** plug on the discovery board.

### Connecting to the firmware console

Connect a USB cable to the **USB USER** mini-B receptacle on the board.
`lsusb` should show you a device with the following ID : 18d1:500f

You can get a console over USB by issuing the following command on a Linux computer:

`echo '18d1 500f' | sudo tee /sys/bus/usb-serial/drivers/generic/new_id`

Testing
-------

Currently, the TCPM is expecting to have a GPIO to detect VBUS, but to minimize the HW setup with the discovery board the alternative is to fake VBUS detection using either the **USER** button on the discovery board, or the `vbus` console command, both of which toggle the state of VBUS detected. For example, to make get a PD contract with a power adapter, plug in the adapter and then toggle VBUS on. When a PD contract above 6V is made, LED5 on the discovery board will light. To disconnect, toggle VBUS off.

EC command line commands

- `help`  List all available EC console commands
- `vbus`  Toggle VBUS on/off
- `pd <port> state`  Print PD protocol state information

Known Issues
------------

1. This doc is not written yet ...

2. You might need a ChromeOS chroot ...

Troubleshooting
---------------

1. OpenOCD is not finding the device.

	1. Check that your USB mini-B cable is connected to the **USB ST-LINK** plug on the discovery board.
	2. What color is the LD1 LED on the board ?

2. You got black smoke

	1. Time to buy a new one.

