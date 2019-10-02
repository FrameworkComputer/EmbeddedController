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

Please duplicate [driver/tcpm/tcpci.c](../../driver/tcpm/tcpci.c) into **driver/tcpm/##chip#name##.c**.
Then update the control logic through I2C there.

In order for your new code to compile, you need to update [driver/build.mk](../../driver/build.mk) with the new file :
`driver-$(CONFIG_USB_PD_TCPM_##CHIP#NAME##)+=tcpm/##chip#name##.o`
then document the new `CONFIG_USB_PD_TCPM_` variable in the [include/config.h](../../include/config.h) file and define it in the board configuration in [board/pdeval-stm32f072/board.h](board.h).

### Board configuration

In [board/pdeval-stm32f072/board.h](board.h), you can update `CONFIG_USB_PD_PORT_MAX_COUNT` to the actual number of ports on your board.
You also need to create/delete the corresponding `PD_Cx` tasks in [board/pdeval-stm32f072/ec.tasklist](ec.tasklist).

By default, the firmware is using I2C1 with SCL/SDA on pins PB6 and PB7, running with a 100kHz clock, and tries to talk to TCPCs at i2c slave addresses 0x9c and 0x9e.
To change the pins or speed, you need to edit `i2c_ports` in [board/pdeval-stm32f072/board.c](board.c), update `I2C_PORT_TCPC` in [board/pdeval-stm32f072/board.h](board.h) with the right controller number, and change the pin mux in [board/pdeval-stm32f072/gpio.inc](gpio.inc). To change TCPC i2c slave addresses, update `TCPC1_I2C_ADDR` and `TCPC2_I2C_ADDR` in [board/pdeval-stm32f072/board.h](board.h).

The I2C bus needs pull-up resistors on SCL/SDA. If your setup doesn't have external pull-ups on those lines, you can activate the chip internal pull-ups (but they are a bit weak for I2C) by editing [board/pdeval-stm32f072/gpio.inc](gpio.inc) and updating the alternate mode configuration flags with `GPIO_PULL_UP` e.g. :
`ALTERNATE(PIN_MASK(B, 0x00c0), 1, MODULE_I2C,   GPIO_PULL_UP) /* I2C MASTER:PB6/7 */`

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
- `pd <port> swap data`  Request data role swap on port
- `pd <port> swap power`  Request power role swap on port
- `i2cscan`  Scan i2c bus for any responsive devices
- `i2cxfer`  Perform an i2c transaction

On the console, you will the PD state machine transitioning through its states with traces like `C0 st5`.
You can always the human readable name of the current state by doing `pd 0 state` returning something like :
`Port C0 CC1, Ena - Role: SNK-UFP State: SNK_DISCOVERY, Flags: 0x0608`
else the numbering of the state is defined in [include/usb_pd.h](../../include/us_pd.h) by the `PD_STATE_` constants.
It should be by default :
```
[0] DISABLED
[1] SUSPENDED
[2] SNK_DISCONNECTED
[3] SNK_DISCONNECTED_DEBOUNCE
[4] SNK_HARD_RESET_RECOVER
[5] SNK_DISCOVERY
[6] SNK_REQUESTED
[7] SNK_TRANSITION
[8] SNK_READY
[9] SNK_SWAP_INIT
[10] SNK_SWAP_SNK_DISABLE
[11] SNK_SWAP_SRC_DISABLE
[12] SNK_SWAP_STANDBY
[13] SNK_SWAP_COMPLETE
[14] SRC_DISCONNECTED
[15] SRC_DISCONNECTED_DEBOUNCE
[16] SRC_ACCESSORY
[17] SRC_HARD_RESET_RECOVER
[18] SRC_STARTUP
[19] SRC_DISCOVERY
[20] SRC_NEGOCIATE
[21] SRC_ACCEPTED
[22] SRC_POWERED
[23] SRC_TRANSITION
[24] SRC_READY
[25] SRC_GET_SINK_CAP
[26] DR_SWAP
[27] SRC_SWAP_INIT
[28] SRC_SWAP_SNK_DISABLE
[29] SRC_SWAP_SRC_DISABLE
[30] SRC_SWAP_STANDBY
[31] SOFT_RESET
[32] HARD_RESET_SEND
[33] HARD_RESET_EXECUTE
[34] BIST_RX
[35] BIST_TX
```

Known Issues
------------

1. This doc is not finished yet ...

2. You might need a ChromeOS chroot ...

Troubleshooting
---------------

1. OpenOCD is not finding the device.

	1. Check that your USB mini-B cable is connected to the **USB ST-LINK** plug on the discovery board.
	2. What color is the LD1 LED on the board ?

1. On the I2C bus, SDA/SCL lines are staying always low

	1. You might be missing some pull-up resistors on the bus.
	1. Check the [Board configuration](#Board-configuration) section if you cannot add external pull-ups.

1. You got black smoke

	1. Time to buy a new one.

