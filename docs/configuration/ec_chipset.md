# Configure EC Chipset

## Config options

The EC chipset is selected using board specific make file [build.mk]. The
following configuration options specify the type and size of flash memory used
by the EC.

  - `CONFIG_SPI_FLASH_REGS` - Should always be defined when using internal or
    external SPI flash.
  - `CONFIG_SPI_FLASH` - Define only if your board uses an external flash.
  - `CONFIG_SPI_FLASH_<device_type>` - Select exactly one the supported flash
    devices to compile in the required driver. This is needed even when using
    the internal SPI flash of the EC chipset.
  - Additional EC Chipset options are prefixed with `CONFIG_HIBERNATE*` and
    should be evaluated for relevance on your board.

## Feature Parameters

  - `CONFIG_FLASH_SIZE <bytes>` - Set to the size of the internal flash of the
    EC. Must be defined to link the final image.
  - `CONFIG_SPI_FLASH_PORT <port>` - Only used if your board as an external
    flash.

## GPIOs and Alternate Pins

Configure the signals which will wakeup the EC from hibernate or deep sleep.
Typical wakeup sources include:

- `GPIO_LID_OPEN` - An active high signal that indicates the lid has been
  opened. The source of the signal is typically from a [GMR](../ec_terms.md#gmr)
  or Hall-Effect sensor. The `GPIO_INT()` entry for this signal should be
  connected to the `lid_interrupt()` routine.
- `GPIO_AC_PRESENT` - A signal from the battery charger that indicates the
  device is connected to AC power. This signal is connected to the
  `power_interrupt()` routine.
- `GPIO_POWER_BUTTON_L` - An active low signal from the power switch. This signal is connected to the `power_button_interrupt()` routine.
- `GPIO_EC_RST_ODL` - On some Nuvoton EC chipsets, the reset signal is
  dual-routed to both a dedicated reset pin and a GPIO. In this case, no
  interrupt handler needs to be registered to the GPIO signal, but the GPIO pin
  must still be configured to wake on both edge types. The GPIO pin should also
  be locked prevent the pin configuration from changing after the EC read-only
  code runs.

See the [GPIO](./gpio.md) documentation for additional details on the GPIO
macros.

## Data structures

- `const enum gpio_signal hibernate_wake_pins[]` - add all GPIO signals that
  should trigger a wakeup of the EC.
- `const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);` -
  configures the number of wake signals used on the board.

All ChromeOS wake sources are documented on the ChromeOS partner site in the
[Wake Sources and Battery Life] section.  The EC specific wake sources are found
under the Deep Sleep and Shipping states and include:

- Power button
- AC insert
- Lid open

## Tasks

None required by this feature.

## Testing and Debugging

## Example

For the Volteer reference board, the following wake sources are defined in
[gpio.inc]. Note that configuration of `GPIO(EC_RST_ODL)` is located after all
`GPIO_INT()` entries required by the board.

```c
/* Wake Source interrupts */
GPIO_INT(EC_LID_OPEN,          PIN(D, 2), GPIO_INT_BOTH | GPIO_HIB_WAKE_HIGH, lid_interrupt)
GPIO_INT(EC_WP_L,              PIN(A, 1), GPIO_INT_BOTH, switch_interrupt)
GPIO_INT(H1_EC_PWR_BTN_ODL,    PIN(0, 1), GPIO_INT_BOTH, power_button_interrupt)
GPIO_INT(ACOK_OD,              PIN(0, 0), GPIO_INT_BOTH | GPIO_HIB_WAKE_HIGH, extpower_interrupt)

/* EC_RST_ODL - PSL input but must be locked */
GPIO(EC_RST_ODL, PIN(0, 2), GPIO_INT_BOTH | GPIO_HIB_WAKE_HIGH | GPIO_LOCKED)
```

For the NPCx7 chipset, the alternate function must also be configured to connect
the wakeup pins to the PSL (power switch logic).

```c
/* GPIOD2 = EC_LID_OPEN */
ALTERNATE(PIN_MASK(D, BIT(2)), 0, MODULE_PMU, 0)
/* GPIO00 = ACOK_OD,
   GPIO01 = H1_EC_PWR_BTN_ODL
   GPIO02 = EC_RST_ODL */
ALTERNATE(PIN_MASK(0, BIT(0) | BIT(1) | BIT(2)), 0, MODULE_PMU, 0)
```

The final step is to add the hibernate signals array to Volteer [baseboard.c] file:

```c
/* Wake up pins */
const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_LID_OPEN,
	GPIO_ACOK_OD,
	GPIO_POWER_BUTTON_L,
	GPIO_EC_RST_ODL,
};
const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);
```
[gpio.inc]: ../../board/volteer/gpio.inc
[baseboard.c]: ../../baseboard/volteer/baseboard.c
[build.mk]: ../new_board_checklist.md#board_build_mk
[Wake Sources and Battery Life]: https://chromeos.google.com/partner/dlm/docs/latest-requirements/chromebook.html#wake-sources-and-battery-life