# Configure I2C Buses

## Config options

The I2C options are prefixed with `CONFIG_I2C*`. Evaluate whether each option is
appropriate to add to your board.

A typical EC and board should at a minimum set `CONFIG_I2C` and
`CONFIG_I2C_MASTER`.

## Feature Parameters

The following parameters control the behavior of the I2C library. [config.h]
defines a reasonable default value, but you may need to change the default value
for your board.

- `CONFIG_I2C_CHIP_MAX_READ_SIZE <bytes>`
- `CONFIG_I2C_NACK_RETRY_COUNT <count>`
- `CONFIG_I2C_EXTRA_PACKET_SIZE <bytes>` - Only used on STM32 EC's if
  `CONFIG_HOSTCMD_I2C_SLAVE_ADDR_FLAGS` is defined.

## GPIOs and Alternate Pins

In the gpio.inc file, you need to define a GPIO for the clock (SCL) and data
(SDA) pin used on each active I2C bus. The corresponding GPIOs are then included
in the `i2c_ports[]` array. This permits the I2C library to perform common bus
recovery actions using bit-banging without involvement by the EC-specific I2C
device driver.

You also need to define the alternate function assignment for all I2C pins using
the `ALTERNATE()` macro.  This step can be skipped for any pins that default to
I2C functionality.

Note that many I2C buses only support 1.8V operation. This is determined by I2C
devices connected to the bus. In this case you need to include `GPIO_SEL_1P8V`
as part of the `flags` field in both the `GPIO()` and `ALTERNATE()` macros. I2C
bus 0 in the example below demonstrates configuring the SCL and SDA pins for
1.8V operation.

See the [GPIO](./gpio.md) documentation for additional details on the GPIO
macros.

## Data Structures

- `const struct i2c_port_t i2c_ports[]` - This array should be defined in your
  baseboard.c or board.c file.  This array defines the mapping of internal I2C
  port numbers used by the I2C library to the physical I2C ports connected to
  the EC.
- `const unsigned int i2c_port_used = ARRAY_SIZE(i2c_ports)` - Defines the
  number of internal I2C ports accessible by the I2C library.

## Tasks

None required by this feature.

## Testing and Debugging

### Console Commands

- `i2cscan` - Provides a quick look of all I2C devices found on all configured
  buses.
- `i2cxfer` - Allows you to read and write individual registers on an I2C
  device.

For runtime troubleshooting of an I2C device, enable and the [I2C
tracing](../i2c-debugging.md) module to log all I2C transactions initiated by
the EC code.

## Example

The image below shows the I2C bus assignment for the Volteer reference board.

![I2C Example]

The `gpio.inc` file for Volteer defines both `GPIO()` and `ALTERNATE()` entries for
all I2C buses used in the design.

```c
/* I2C pins - Alternate function below configures I2C module on these pins */
GPIO(EC_I2C0_SENSOR_SCL,       PIN(B, 5), GPIO_INPUT | GPIO_SEL_1P8V)
GPIO(EC_I2C0_SENSOR_SDA,       PIN(B, 4), GPIO_INPUT | GPIO_SEL_1P8V)
GPIO(EC_I2C1_USB_C0_SCL,       PIN(9, 0), GPIO_INPUT)
GPIO(EC_I2C1_USB_C0_SDA,       PIN(8, 7), GPIO_INPUT)
GPIO(EC_I2C2_USB_C1_SCL,       PIN(9, 2), GPIO_INPUT)
GPIO(EC_I2C2_USB_C1_SDA,       PIN(9, 1), GPIO_INPUT)
GPIO(EC_I2C3_USB_1_MIX_SCL,    PIN(D, 1), GPIO_INPUT)
GPIO(EC_I2C3_USB_1_MIX_SDA,    PIN(D, 0), GPIO_INPUT)
GPIO(EC_I2C5_POWER_SCL,        PIN(3, 3), GPIO_INPUT)
GPIO(EC_I2C5_POWER_SDA,        PIN(3, 6), GPIO_INPUT)
GPIO(EC_I2C7_EEPROM_SCL,       PIN(B, 3), GPIO_INPUT)
GPIO(EC_I2C7_EEPROM_SDA,       PIN(B, 2), GPIO_INPUT)

/* Alternate functions GPIO definitions */
ALTERNATE(PIN_MASK(B, BIT(5) | BIT(4)), 0, MODULE_I2C, (GPIO_INPUT | GPIO_SEL_1P8V)) /* I2C0 */
ALTERNATE(PIN_MASK(9, BIT(0) | BIT(2) | BIT(1)), 0, MODULE_I2C, 0)                   /* I2C1 SCL / I2C2 */
ALTERNATE(PIN_MASK(8, BIT(7)), 0, MODULE_I2C, 0)                                     /* I2C1 SDA */
ALTERNATE(PIN_MASK(D, BIT(1) | BIT(0)), 0, MODULE_I2C, 0)                            /* I2C3 */
ALTERNATE(PIN_MASK(3, BIT(3) | BIT(6)), 0, MODULE_I2C, 0)                            /* I2C5 */
ALTERNATE(PIN_MASK(B, BIT(3) | BIT(2)), 0, MODULE_I2C, 0)                            /* I2C7 */
```

The `i2c_ports[]` array requires the `.port` field to be assigned to an EC
chipset specific enumeration. For the NPCx7 I2C bus names are defined in
[./chip/npcx/registers.h]. The Volteer `baseboard.h` file creates a mapping
from the schematic net name to the NPCx7 I2C bus enumeration.

```c
#define CONFIG_I2C
#define I2C_PORT_SENSOR		NPCX_I2C_PORT0_0
#define I2C_PORT_USB_C0		NPCX_I2C_PORT1_0
#define I2C_PORT_USB_C1		NPCX_I2C_PORT2_0
#define I2C_PORT_USB_1_MIX	NPCX_I2C_PORT3_0
#define I2C_PORT_POWER		NPCX_I2C_PORT5_0
#define I2C_PORT_EEPROM		NPCX_I2C_PORT7_0
```

The last piece for I2C configuration is to create the `i2c_ports[]` array using
the macros and enumerations added to `baseboard.h` and `gpio.inc`.

```c
/* I2C port map configuration */
const struct i2c_port_t i2c_ports[] = {
	{
		.name = "sensor",
		.port = I2C_PORT_SENSOR,
		.kbps = 400,
		.scl = GPIO_EC_I2C0_SENSOR_SCL,
		.sda = GPIO_EC_I2C0_SENSOR_SDA,
		.flags = 0,
	},
	{
		.name = "usb_c0",
		.port = I2C_PORT_USB_C0,
		/*
		 * I2C buses used for PD communication must be set for 400 kbps
		 * or greater. Set to the maximum speed supported by all devices.
		 */
		.kbps = 1000,
		.scl = GPIO_EC_I2C1_USB_C0_SCL,
		.sda = GPIO_EC_I2C1_USB_C0_SDA,
	},
	{
		.name = "usb_c1",
		.port = I2C_PORT_USB_C1,
		/*
		 * I2C buses used for PD communication must be set for 400 kbps
		 * or greater. Set to the maximum speed supported by all devices.
		 */
		.scl = GPIO_EC_I2C2_USB_C1_SCL,
		.sda = GPIO_EC_I2C2_USB_C1_SDA,
	},
	{
		.name = "usb_1_mix",
		.port = I2C_PORT_USB_1_MIX,
		.kbps = 100,
		.scl = GPIO_EC_I2C3_USB_1_MIX_SCL,
		.sda = GPIO_EC_I2C3_USB_1_MIX_SDA,
	},
	{
		.name = "power",
		.port = I2C_PORT_POWER,
		.kbps = 100,
		.scl = GPIO_EC_I2C5_POWER_SCL,
		.sda = GPIO_EC_I2C5_POWER_SDA,
	},
	{
		.name = "eeprom",
		.port = I2C_PORT_EEPROM,
		.kbps = 400,
		.scl = GPIO_EC_I2C7_EEPROM_SCL,
		.sda = GPIO_EC_I2C7_EEPROM_SDA,
	},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);
```

The `.flags` field is optional when using the default I2C bus setup. See
[./include/i2c.h] for the full list of supported flags.

The flag `I2C_PORT_FLAG_DYNAMIC_SPEED` allows the I2C bus frequency to be
changed at runtime. The typical use case is to set the I2C bus frequency to
different speeds based on the BOARD_VERSION in [CBI]. For example board version
1 supports 100 kbps operation but board version 2 and greater supports 400 kbps
operation. `I2C_PORT_FLAG_DYNAMIC_SPEED` is not used to change the I2C bus
frequency on the fly depending on the addressed slave device.

An example of changing the I2C bus frequency from the [Kodama
board](../../board/kodama/board.c) is shown below.

```c
static void board_i2c_init(void)
{
	if (board_get_version() < 2)
		i2c_set_freq(1,  I2C_FREQ_100KHZ);
}
DECLARE_HOOK(HOOK_INIT, board_i2c_init, HOOK_PRIO_INIT_I2C);
```


[config.h]: ../new_board_checklist.md#config_h
[./chip/npcx/registers.h]: ../../chip/npcx/registers.h
[./include/i2c.h]: ../../include/i2c.h
[I2C Example]: ../images/i2c_example.png
[CBI]: https://chromium.googlesource.com/chromiumos/docs/+/master/design_docs/cros_board_info.md
