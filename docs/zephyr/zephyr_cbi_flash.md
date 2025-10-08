# Zephyr CBI In Flash Configuration

[TOC]

## Overview

The CrOS Board Information ([`CBI`]) data can be stored in a reserved region of
the EC's internal flash memory. This removes the need for an external EEPROM
chip, which reduces the BOM cost, simplifies hardware design, and frees up I2C
bus resources.

The CBI data is stored in the write-protected (RO) portion of the flash by
defining a dedicated `CBI` section in the firmware map (FMAP). This FMAP entry
allows flashing tools to identify the region and apply safeguards, preventing
the data from being accidentally erased during routine firmware updates.

## Kconfig Options

General CBI support (`CONFIG_PLATFORM_EC_CBI`) is enabled by default for Zephyr
EC projects.

The `CONFIG_PLATFORM_EC_CBI_FLASH` Kconfig option enables CBI support using
flash storage. This option is enabled automatically when a devicetree node with
the `cbi_flash` nodelabel is present and enabled in the final device tree.
You do not need to set this Kconfig option manually in your project's
configuration.

## Devicetree Nodes

The size and location of the CBI region within the EC flash are defined using a
`binman` node in the devicetree. This is typically done in a board-specific
`binman.dtsi` file.

The CBI region is defined as a child node within the `wp-ro` node.

An example definition for the CBI flash region:

```c
/* From include/cros/cbi_flash.dtsi */
&binman {
	wp-ro {
		cbi_flash: cbi {
			compatible = "cros-ec,flash-layout";
			type = "fill";
			offset = <0x40000>;
			size = <0>;
			fill-byte = [ff];
			image-size = <256>;
			preserve;
		};
	};
};
```
-   `offset`, `size`: Define the location and size of the CBI region within
    `wp-ro`. A size of 4 KB (`0x1000`) is typical. The offset must be carefully
    calculated to place the CBI region correctly, often at the end of the
    `EC_RO` section.
-   `preserve`: This crucial property flags the region to be preserved across
    firmware updates when using tools like `flash_ec`.

For a complete board example, the `rex` program includes the common CBI flash
layout from `<cros/cbi_flash.dtsi>` and then overrides the specific `offset`
and `size` in its overlay file. This places a 4 KB CBI region at the end of its
EC-RO firmware section.


```c
/* From program/rex/rex/project.overlay */
#include <cros/cbi_flash.dtsi>

&cbi_flash {
	/*
	 * CBI section size is 0x1000 bytes, the CBI portion lies at the end of
	 * EC_RO section. EC_RO section ends at 0x50000 byte which is the offset
	 */
	offset = <0x50000>;
	size = <0x1000>;
};
```

## Flashing and Data Preservation

The CBI flash region is protected from accidental writes:

*   **Using `flash_ec`:** The `flash_ec` utility, when used for flashing, reads
    the existing flash content, preserves the CBI data from the on-device image,
    and merges it into the new firmware image before writing. This is enabled by
    the `preserve` property in the devicetree.
*   **Using `flashrom` from the AP:** The EC's host command handler for flash
    operations (`EC_CMD_FLASH_WRITE`, `EC_CMD_FLASH_ERASE`) is designed to
    reject any attempts to write or erase the memory range corresponding to the
    CBI region. This makes `flashrom` operations from the host safe.

To intentionally overwrite the CBI data (e.g., during initial factory
provisioning), use the `--no-preserve` flag with `flash_ec`.

## Testing and Debugging

The standard `cbi` console command and `ectool cbi` host command work
transparently with CBI data stored in flash. There is no difference in usage
compared to other storage backends.

[`CBI`]: ./zephyr_cbi.md
[`Kconfig.cbi`]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.cbi
