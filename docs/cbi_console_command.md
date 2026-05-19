# `cbi` Console Command Documentation

The `cbi` console command is used to inspect, modify, or reinitialize the
Chromium OS Board Information (CBI) stored in the EC's non-volatile storage
(EEPROM or internal flash).

## Syntax

```text
cbi
cbi set <tag> <value> <size> [init | skip_write]
cbi set <tag> <hex_string> [init | skip_write]  (For UFSC)
cbi remove <tag> [init | skip_write]
```

## Subcommands and Usage

### 1. Query CBI Data (`cbi`)

Running the command without any arguments dumps all parsed CBI fields and
displays a hexadecimal dump of the raw CBI structure currently cached in memory.

**Example Output:**
```text
> cbi
CBI_VERSION: 0x0000
TOTAL_SIZE: 60
BOARD_VERSION: 1 (0x1)
OEM_ID: 2 (0x2)
MODEL_ID: 5 (0x5)
SKU_ID: 0 (0x0)
FW_CONFIG: 0 (0x0)
PCB_SUPPLIER: 0 (0x0)
SSFC: 0 (0x0)
REWORK_ID: 0 (0x0)
UFSC: (Error -3)
00000000  5f 43 42 49 3c 00 00 00  7f 00 01 00 01 00 00 01  |_CBI<...........|
00000010  01 00 01 00 02 00 00 05  01 00 01 00 00 00 00 00  |................|
...
```

### 2. Set CBI Field (`cbi set ...`)

Sets a specific CBI tag to a given value.

*   **Standard Fields:** `cbi set <tag> <value> <size>`
    *   `<tag>`: The integer ID of the CBI tag (see [CBI Tags
        Reference](#cbi-tags-reference)).
    *   `<value>`: The value to set (can be decimal or hex with `0x` prefix).
    *   `<size>`: The size of the field in bytes (typically 1, 2, or 4).
    *   *Example*: `cbi set 2 0x12 4` (Sets SKU ID to 0x12, size 4 bytes).

*   **String Fields (DRAM Part Number, OEM Name):** `cbi set <tag> <string>
    <size_arg>`
    *   Although the size is determined by the string length, a size argument
        must still be provided to satisfy command parsing (it will be ignored).
    *   *Example*: `cbi set 3 "DRAM_PART_XYZ" 0`

*   **Unified Firmware and Second-source Config (UFSC):** `cbi set 29
    <128-bit_hex_string>`
    *   UFSC (Tag 29) expects a 32-character hex string (representing 16 bytes /
        128 bits).
    *   *Example*: `cbi set 29 0102030405060708090a0b0c0d0e0f10`

> **NOTE**: After modifying CBI data using `cbi set` (or `cbi remove`), it is
> highly recommended to **reboot the EC** (e.g., using the `reboot` console
> command). Many EC subsystems and drivers query CBI data only once during
> boot-time initialization (e.g., in `HOOK_INIT`). Rebooting ensures that the EC
> runtime setup is fully synchronized with the updated CBI data.

### 3. Remove CBI Field (`cbi remove <tag>`)

Removes a specific tag from the CBI structure.

*   *Example*: `cbi remove 6` (Removes FW_CONFIG).

---

## Creating the UFSC Value (Tag 29)

The Unified Firmware and Second-source Configuration (UFSC) value is a 128-bit (16-byte) value.

1.  **Source of Truth**: The UFSC configuration is defined in the **Boxster** configuration system and stored in a JSON proto file (typically under the program's configuration directory).
2.  **Format in JSON**: In the JSON configuration, it is represented as an array of four 32-bit decimal integers under the `unifiedFwConfig` key.
    *   *Example*:
        ```json
        "unifiedFwConfig": {
          "value": [
            105123905,
            2113,
            0,
            0
          ]
        }
        ```
3.  **Conversion to Hex for `cbi` Command**:
    To use this value with the `cbi set 29` command, you must convert each of the four decimal words to hexadecimal, **endian-swap** each word, and concatenate them into a single 32-character hexadecimal string.

    **Conversion Steps for the Example:**
    *   **Word 0**: `105123905` (decimal) $\rightarrow$ `0x06441041` (hex) $\rightarrow$ Endian-swapped: `41104406`
    *   **Word 1**: `2113` (decimal) $\rightarrow$ `0x00000841` (hex) $\rightarrow$ Endian-swapped: `41080000`
    *   **Word 2**: `0` (decimal) $\rightarrow$ `0x00000000` (hex) $\rightarrow$ Endian-swapped: `00000000`
    *   **Word 3**: `0` (decimal) $\rightarrow$ `0x00000000` (hex) $\rightarrow$ Endian-swapped: `00000000`

    Concatenating these four swapped words yields the final 32-character string:
    `41104406410800000000000000000000`

    **Command Example:**
    ```bash
    cbi set 29 41104406410800000000000000000000
    ```
    *(Note: Ensure you provide the full 32-character string, including trailing zeros for zero-valued words, to satisfy the 16-byte size requirement).*

---

## Optional Flags

Both `set` and `remove` subcommands accept optional trailing flags:

### `init`
Initializes a new CBI structure in memory before applying the change.
*   **When to use:**
    *   **Factory Provisioning:** Use `init` when the CBI storage is completely
        empty, corrupted, or needs to be overwritten from scratch.
    *   **Restructuring:** If you want to clear all existing tags and start with
        only the tag specified in the current command.
*   **Behavior:** It zeroes out the in-memory CBI buffer, writes the CBI magic
    signature (`_CBI`), resets the total size to the header size, and then
    applies the `set` or `remove` operation.
*   *Example*: `cbi set 0 1 1 init` (Initializes CBI and sets Board Version to
    1).

> [!IMPORTANT] The `init` flag is subject to security restrictions. See
> [Security Restrictions
> (CONFIG_SYSTEM_UNLOCKED)](#security-restrictions-config_system_unlocked)
> below.

### `skip_write`
Performs the operation only on the in-memory CBI cache and skips writing the
changes back to the non-volatile storage (EEPROM/Flash).
*   **When to use:** For testing configuration changes temporarily without
    committing them to physical storage.
*   *Example*: `cbi set 6 0x100 4 skip_write`

---

## Security Restrictions (CONFIG_SYSTEM_UNLOCKED)

The `init` flag's behavior is heavily guarded to prevent unauthorized or
accidental erasure of board info on production devices.

### If `CONFIG_SYSTEM_UNLOCKED` is NOT defined (Locked / Production Mode)
*   The `init` operation is **only allowed if the existing CBI data in storage
    is invalid or corrupted**.
*   If the EC detects a **valid** CBI structure in storage, the `init` command
    will be rejected, returning `ACCESS_DENIED` ("Failed to init. System
    locked").
*   This ensures that once a device is provisioned in the factory, its CBI
    cannot be easily wiped or reset via the console.

### If `CONFIG_SYSTEM_UNLOCKED` is defined (Unlocked / Developer Mode)
*   The `init` operation is **always allowed**.
*   You can reinitialize the CBI and overwrite existing valid data at any time.
*   This is typically used during early board bring-up and development.

### Write Protection (WP)
Independent of `CONFIG_SYSTEM_UNLOCKED`, if hardware write protection is enabled
on the flash/EEPROM, any write to physical storage will fail unless `skip_write`
is specified.

---

## CBI Tags Reference

The authoritative list of CBI tags is defined in [ec_commands.h]. Below is a
reference of common tags:

| Tag ID | Constant | Description |
| :--- | :--- | :--- |
| `0` | `CBI_TAG_BOARD_VERSION` | Hardware board version. |
| `1` | `CBI_TAG_OEM_ID` | OEM Identifier. |
| `2` | `CBI_TAG_SKU_ID` | SKU Identifier. |
| `3` | `CBI_TAG_DRAM_PART_NUM`| DRAM part number (string). |
| `4` | `CBI_TAG_OEM_NAME` | OEM Name (string). |
| `5` | `CBI_TAG_MODEL_ID` | Model Identifier. |
| `6` | `CBI_TAG_FW_CONFIG` | Firmware configuration bitfield. |
| `7` | `CBI_TAG_PCB_SUPPLIER` | PCB Supplier ID. |
| `8` | `CBI_TAG_SSFC` | Second Source Factory Cache. |
| `9` | `CBI_TAG_REWORK_ID` | Rework Identifier (64-bit). |
| `12-27` | `CBI_TAG_BATTERY_CONFIG`| Battery configurations. |
| `28` | `CBI_TAG_PROVISION_MATRIX_VERSION`| Provision matrix version. |
| `29` | `CBI_TAG_UFSC` | Unified Firmware & Second-source Config (128-bit). |

[ec_commands.h]: ../include/ec_commands.h
