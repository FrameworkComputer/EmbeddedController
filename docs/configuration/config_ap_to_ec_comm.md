# Configure AP to EC Communication

This document provides details on how to configure the AP to EC communication
channel used on your board.  The [AP to EC Communication] document provides
details a system level of the operation of this feature.

## Config options

Configure the AP to EC communication channel, picking exactly one of the
following options.

- `CONFIG_HOSTCMD_SPS` - [SPI slave](./ec_terms.md#spi) (SPS) interface
- `CONFIG_HOSTCMD_HECI` - HECI interface
- `CONFIG_HOSTCMD_LPC` - [LPC](./ec_terms.md#lpc) bus
- `CONFIG_HOSTCMD_ESPI` - [eSPI](./ec_terms.md#espi) bus

In [config.h], search for options that start with the same name as your selected
communication interface.  Override defaults as needed.

## Feature Parameters

None needed in this section.

## GPIOs and Alternate Pins

The EC code requires the following signals between the AP and the EC to be
defined by each board variant.

- `GPIO_ENTERING_RW` - Output from the EC, active high signal indicates when the
  EC code transitions from RO to RW code.

  ```c
  GPIO(EC_ENTERING_RW,           PIN(E, 3), GPIO_OUT_LOW)
  ```

- `GPIO_SYS_RESET_L` - Output from the EC, active low signal used to put the AP
  into reset.

  ```c
  GPIO(SYS_RST_ODL,              PIN(C, 5), GPIO_ODR_HIGH)
  ```

Create `ALTERNATE()` entries for all EC signals used for AP communication. This
step can be skipped for any pins that default to communication channel
functionality.

See the [GPIO](./gpio.md) documentation for additional details on
the GPIO macros.

## Data structures

None needed in this section.

## Tasks

The `HOSTCMD` task is responsible for processing commands sent by the AP and is
always required. The typical priority is higher than the `CHIPSET` task, but
lower than the `CONSOLE` task.

```c
	TASK_ALWAYS(HOSTCMD, host_command_task, NULL, LARGER_TASK_STACK_SIZE, 0) \
```

## Testing and Debugging

For Nuvoton EC chipsets, the file [./chip/npcx/registers.h] provides a
collection of `DEBUG_*` macros that can be used to enable extra console messages
related to a specific interface.  For AP to EC communication, the `DEBUG_LPC`
and `DEBUG_ESPI` macros can help troubleshoot communication issues.

[./chip/npcx/registers.h]: ../../chip/npcx/registers.h
[AP to EC Communication]: ../ap-ec-comm.md
[config.h]: ../new_board_checklist.md#config_h
