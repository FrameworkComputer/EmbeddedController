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

The CBI contains information that the firmware uses at runtime to adapt its
behavior to the specific hardware present on a device. Different mechanisms
exist depending on the age of the device platform.

#### Legacy FW_CONFIG and SSFC

Legacy devices utilize two distinct mechanisms for firmware configuration:
*   **Firmware Configuration ([`FW_CONFIG`]):** A 32-bit field that stores
    non-probeable characteristics tied to a specific SKU, such as the
    presence of a backlit keyboard.
*   **Second Source Factory Cache ([`SSFC`]):** A 32-bit field used to handle
    probeable, second-source components (e.g., different codecs, sensors)
    that do not affect the device's SKU.

#### Unified Firmware and Second-source Configuration (UFSC)

Newer devices use a new unified system called **Unified Firmware and
Second-source Configuration ([`UFSC`])**. This system replaces the separate
`FW_CONFIG` and `SSFC` fields with a single, schema-driven 128-bit (4-DWORD)
value.

## Kconfig Options

Refer to [`Kconfig.cbi`] for all the Kconfig options that control [`CBI`]
behavior. The appropriate configuration system (UFSC, FW_CONFIG, or SSFC) is
typically enabled automatically based on the devicetree configuration.

## Testing and Debugging

The [`ectool cbi`] command can be run from the kernel to get/set [`FW_CONFIG`]
and [`SSFC`] values.  The EC console has a [`cbi` console command] that can be
used to inspect or modify CBI data directly.


[`CBI`]: https://chromium.googlesource.com/chromiumos/docs/+/HEAD/design_docs/cros_board_info.md
[`ectool cbi`]: ./zephyr_cbi.md#testing-and-debugging
[`cbi` console command]: ../cbi_console_command.md
[`EEPROM`]: ./zephyr_eeprom.md
[`CBI In Flash`]: ./zephyr_cbi_flash.md
[`FW_CONFIG`]: ./zephyr_fw_config.md
[`SSFC`]: ./zephyr_ssfc.md
[`UFSC`]: ./zephyr_ufsc.md
[`Kconfig.cbi`]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.cbi
