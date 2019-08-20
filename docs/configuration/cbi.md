# Configure CrOS Board Information (CBI)

If your board includes an EEPROM to store [CBI], then this feature must be
enabled and configured. Note that the [I2C buses] must be configured and working
before enabling CBI.

## Config options

Add the following config options to `baseboard.h` or `board.h`.

- `CONFIG_BOARD_VERSION_CBI`
- `CONFIG_CROS_BOARD_INFO`

## Feature Parameters

- `I2C_ADDR_EEPROM_FLAGS <7-bit addr>` - Defines the 7-bit slave address for the
  EEPROM containing CBI.

## GPIOs and Alternate Pins

None needed - the I2C pins should be configured automatically when initializing
the I2C buses.

## Data Structures

None required by this feature.

## Tasks

None required by this feature.

## Testing and Debugging

Refer to the [I2C debugging information] to verify communication with the CBI EEPROM.

[CBI]: https://chromium.googlesource.com/chromiumos/docs/+/master/design_docs/cros_board_info.md
[I2C buses]: ./i2c.md
[I2C debugging information]: ./i2c.md#