# GPIO Configuration

GPIO setup is done for every board variant, but never for the baseboard, by
configuring the file `./board/<board>/gpio.inc`. This file configures all the
the pins on the EC chipset through the following macros.

- `GPIO(<name>, ...)` - Configures simple GPIO input and outputs
- `GPIO_INT(<name>, ...)` - Configures GPIO inputs that connect to an interrupt
  service routine.  Historically these entries are defined first, but this no
  longer required.
- `ALTERNATE(...)` - Configures a pin for an alternate function (e.g I2C, ADC,
  SPI, etc)
- `UNIMPLEMENTED(<name>, ...)` - Creates a fake GPIO entry

The `GPIO()`, `GPIO_INT()`, and `UNIMPLEMENTED()` macros create a C enumeration
of the form `GPIO_<name>` that can be used in the code. As noted in [GPIO
Naming](../new_board_checklist.md#GPIO-Naming), the `<name>` parameter should
always match the schematic net name.

## `GPIO()` macro

### Prototype

`GPIO(name, pin, flags)`

- `name` - Defines the schematic net name, which is expanded to the enumeration
  `GPIO_name` by the macro.
- `pin` - Use the `PIN(group,pin)` macro to define the GPIO group and pin
  number. Note that on a few EC chipsets, the PIN macro is just `PIN(pin)`.
- `flags` - Define attributes of the pin (direction, pullup/pulldown, open
  drain, voltage level, etc).  All supported flags are found following the
  `GPIO_FLAG_NONE` definition in [./include/gpio.h](../../include/gpio.h).

### Example

![GPIO Example]

```c
GPIO(EC_ENTERING_RW, PIN(E, 3), GPIO_OUT_LOW)
```

The EC common code requires the enum `GPIO_ENTERING_RW` to be defined, so you
should also map the net name to the EC name in the `board.h` file.

```c
#define GPIO_ENTERING_RW    GPIO_EC_ENTERING_RW
```

## `GPIO_INT()` macro

### Prototype
`GPIO_INT(name, pin, flags, signal)`

- `name` - Defines the schematic net name, which is expanded to the enumeration
  `GPIO_name` by the macro.
- `pin` - Same definition as `GPIO()` macro.
- `flags` - Same definition as `GPIO()` macro. Should always have one of the
  `GPIO_INT_*` flags set.
- `signal` - Interrupt service routine called when the pin asserts according to
  the flags set.

### Example

![GPIO_INT Example]

```c
GPIO_INT(EC_LID_OPEN, PIN(D, 2), GPIO_INT_BOTH | GPIO_HIB_WAKE_HIGH, lid_interrupt)
```

The EC common code requires the enum `GPIO_LID_OPEN` to be defined, so you als
need to map the net name to the EC name in the `board.h` file.

```c
#define GPIO_LID_OPEN       GPIO_EC_LID_OPEN
```

## `ALTERNATE()` macro

### Prototype
`ALTERNATE(pinmask, function, module, flags)`

- `pinmask` - Defines a set of pins in the same GPIO group to assign to a
  different function.
- `function` - A chip-specific function number. Only used if the EC chipset
  provides multiple alternate functions in addition to GPIO (e.g. pin can be
  UART, I2C, SPI, or GPIO). The permitted values for this parameter vary based
  on the EC chipset type.
  - STM32 - 0 to 7
  - Maxim - 1 to 3
  - Microchip - 0 to 3
  - MediaTek - 0 to 7
  - All others (Nuvton, ITE, TI Stellaris, ) only support one alternate
    function per pin, so this parameter should be set to 0.
- `module` - One of the enum module_id values defined in
  [./include/module_id.h](../../include/module_id.h).
- `flags` - Same definition as `GPIO()` macro.

### Notes

At runtime there are two mechanisms for switching a pin between GPIO mode and
alternate function mode.

- `gpio_config_module(enum module_id id, int enable)` - Configures all pins
  matching the module enumeration `id`.
- `gpio_config_pin(enum module_id id, enum gpio_signal signal, int enable)` -
  Configures a single pin matching the GPIO enumeration `signal`.

For both routines, if `enable` is 1, then the corresponding pins are configured
for alternate mode operation.  If `enable` is 0, then the corresponding pins are
configure for GPIO mode.

`gpio_config_module()` is automatically called at runtime for all enabled
interfaces (I2C, SPI, UART, etc). You can use `gpio_config_pin()` to temporarily
configure a pin for GPIO operation, and to restore the original alternate
function.  The I2C bus error recovery employs this mechanism to temporarily
driver the I2C SCL and SDA signals to known states, without interference by the
I2C controller in the EC chipset.

The general recipe for overriding alternate functions is shown below.

```c
    /* Disconnect I2C1_SDA pin from I2C controller */
    gpio_config_pin(MODULE_I2C, GPIO_I2C1_SDA, 0);

    /* Setup I2C1_SDA as an GPIO open drain output and drive initial state low */
    gpio_set_flags(GPIO_I2C1_SDA, GPIO_ODR_LOW);

    /* Set GPIO high (or low) as required */
    gpio_set_level (GPIO_I2C1_SDA, 1);

    /* Restore I2C1_SDA pin to I2C function */'
    gpio_config_pin(MODULE_I2C, GPIO_I2C1_SDA, 1);
```



### Example

![ALTERNATE Example]

```c
ALTERNATE(PIN_MASK(B, BIT(4) | BIT(5)), 0, MODULE_I2C, (GPIO_INPUT | GPIO_SEL_1P8V))
```

<!-- Images -->

<!-- If you make changes to the docs below make sure to regenerate the PNGs by
     appending "export/png" to the Google Drive link. -->

<!-- https://docs.google.com/drawings/d/18cWTYQRRCpypYDOLlvKQJTObwcj6wOjUga02B0oZXBg -->
[GPIO Example]: ../images/gpio_example.png
<!-- https://docs.google.com/drawings/d/1X6p5XfB6BBmUUKCrwOg56Bz6LZj9P_WPQXsOdk-OIiI -->
[GPIO_INT Example]: ../images/gpio_int_example.png
<!-- https://docs.google.com/drawings/d/1-kroVezQuA_KdQLzqYPs8u94EBg37z3k6lKzkSLRv-0 -->
[ALTERNATE Example]: ../images/alternate_example.png
