# Zephyr CrOS Board Information (CBI) Configuration

[TOC]

## Overview

CrOS Board Info [`CBI`] is used to store static board information,
such as BOARD_VERSION, SKU_ID and configuration information. With [`CBI`],
the information is either hard coded as GPIO values or is stored in a
writable [`EEPROM`] chip, so we can provision the correct SKU at RMA time.

Kconfig has a CONFIG_PLATFORM_EC_CBI_STORAGE_TYPE choice that lets the
user select GPIOs or an EEPROM as the source of [`CBI`] data.

If your board uses GPIOs to hard code [`CBI`], then
`CONFIG_PLATFORM_EC_CBI_GPIO` must be selected and the [`CBI`] data will be
emulated and only the BOARD_VERSION and SKU_ID [`CBI`] fields are available.

If your board includes an [`EEPROM`] to store [`CBI`], then
`CONFIG_PLATFORM_EC_CBI_EEPROM` must be selected and configured.

[`EEPROM`] [`CBI`] includes two additional pieces of firmware relevant
configuration information that are programmed during manufacturing.

1) The Firmware Configuration [`FW_CONFIG`] stores information
specifically for the firmware, such as whether the device has a backlit
keyboard.  One can view [`FW_CONFIG`] as the firmware characteristic of a
SKU, so a SKU only maps to a single [`FW_CONFIG`], but different SKUs can
map to the same [`FW_CONFIG`].
2) The Second Source Factory Cache [`SSFC`] also stores information about
the device for the firmware to read. The [`SSFC`] describes later decisions
for a board to indicate alternate second sourced hardware stuffing which
can be used by the EC to know which drivers to load.

The difference between [`SSFC`] and [`FW_CONFIG`] is that [`SSFC`] doesn’t
affect SKU. This prevents SKU explosion when a device has many second
source components.

If a Second Source Component is probeable, this should be stored in
[`SSFC`], which avoids creating a new SKU.  If it is not probeable,
it must be added to [`FW_CONFIG`].

## Kconfig Options

Refer to [`Kconfig.cbi`] for all the Kconfig options that control [`CBI`]
behavior.

## Testing and Debugging

The [`ectool cbi`] command can be run from the kernel to get/set [`FW_CONFIG`]
and [`SSFC`] values.  The console has a "cbi" command that can be used to do
the same thing from the EC console.


[`CBI`]: https://chromium.googlesource.com/chromiumos/docs/+/HEAD/design_docs/cros_board_info.md
[`ectool cbi`]: ./zephyr_cbi.md#testing-and-debugging
[`EEPROM`]: ./zephyr_eeprom.md
[`FW_CONFIG`]: ./zephyr_fw_config.md
[`SSFC`]: ./zephyr_ssfc.md
[`Kconfig.cbi`]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.cbi
