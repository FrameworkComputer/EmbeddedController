# Zephyr CrOS Board Information (CBI) Configuration

[TOC]

## Overview

CrOS Board Info [`CBI`] is used to store static board information,
such as BOARD_VERSION, SKU_ID and configuration information. This
information allows a single firmware image to support multiple
hardware variants.

### Storage Backends

CBI data can be stored in several ways:

*   **EC Flash (Recommended):** On recent devices, CBI data is stored in a
    reserved region of the EC's internal flash memory. This approach
    simplifies the hardware design and reduces cost by eliminating the
    need for an external EEPROM chip. For detailed information, see the
    [`CBI In Flash`] documentation.
*   **External EEPROM (Deprecated):** Older devices stored CBI data in a
    dedicated external EEPROM chip on the I2C bus. While this provides more
    storage than GPIOs, it adds to the BOM cost and hardware complexity.
    See the [`EEPROM`] documentation for details.
*   **GPIO Strapping (Deprecated):** The simplest method, used on some
    older cost-sensitive devices, involves reading GPIO values (strapping
    resistors) to determine a limited set of information, typically just
    the BOARD_VERSION and SKU_ID. This method is highly constrained in the
    amount of data it can store.

### Firmware-Relevant Configuration

The CBI data contains two key pieces of firmware-relevant configuration
information that are programmed during manufacturing:

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
[`CBI In Flash`]: ./zephyr_cbi_flash.md
[`FW_CONFIG`]: ./zephyr_fw_config.md
[`SSFC`]: ./zephyr_ssfc.md
[`Kconfig.cbi`]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.cbi
