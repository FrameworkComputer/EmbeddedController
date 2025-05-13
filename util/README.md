Framework specific host commands:

| CMD                                | ID     | Need? | ectool | framework-system |
| EC_CMD_FLASH_NOTIFIED              | 0x3E01 |       |        | Yes              |
| EC_CMD_FACTORY_MODE                | 0x3E02 |       |        | No               |
| EC_CMD_CHARGE_LIMIT_CONTROL        | 0x3E03 |       |        | Yes              |
| EC_CMD_PWM_GET_FAN_ACTUAL_RPM      | 0x3E04 |       |        | No               |
| EC_CMD_SET_AP_REBOOT_DELAY         | 0x3E05 |       |        | No               |
| EC_CMD_ME_CONTROL                  | 0x3E06 |       |        | No               |
| EC_CMD_NON_ACPI_NOTIFY             | 0x3E07 |       |        | No               |
| EC_CMD_DISABLE_PS2_EMULATION       | 0x3E08 | Yes   | Yes    | Yes              |
| EC_CMD_CHASSIS_INTRUSION           | 0x3E09 | Yes   | Yes    | Yes              |
| EC_CMD_BB_RETIMER_CONTROL          | 0x3E0A |       |        | No               |
| EC_CMD_DIAGNOSIS                   | 0x3E0B |       |        | No               |
| EC_CMD_UPDATE_KEYBOARD_MATRIX      | 0x3E0C |       |        | No               |
| EC_CMD_VPRO_CONTROL                | 0x3E0D |       |        | No               |
| EC_CMD_FP_LED_LEVEL_CONTROL        | 0x3E0E | Yes   | Yes    | Yes              |
| EC_CMD_CHASSIS_OPEN_CHECK          | 0x3E0F | Yes   | Yes    | Yes              |
| EC_CMD_ACPI_NOTIFY                 | 0x3E10 |       |        | No               |
| EC_CMD_READ_PD_VERSION             | 0x3E11 | Yes   | Yes    | Yes              |
| EC_CMD_STANDALONE_MODE             | 0x3E13 |       |        | No               |
| EC_CMD_PRIVACY_SWITCHES_CHECK_MODE | 0x3E14 | Yes   | Yes    | Yes              |
| EC_CMD_CHASSIS_COUNTER             | 0x3E15 |       |        | Yes              |
| EC_CMD_CHECK_DECK_STATE            | 0x3E16 | Yes   | No     | Yes              |
| EC_CMD_GET_SIMPLE_VERSION          | 0x3E17 | Yes   | Yes    | No               |
| EC_CMD_GET_ACTIVE_CHARGE_PD_CHIP   | 0x3E18 |       |        | Yes              |
| EC_CMD_UEFI_APP_MODE               | 0x3E19 |       |        | Yes              |
| EC_CMD_UEFI_APP_BTN_STATUS         | 0x3E1A |       |        | Yes              |
| EC_CMD_EXPANSION_BAY_STATUS        | 0x3E1B |       |        | Yes              |
| EC_CMD_GET_HW_DIAG                 | 0x3E1C |       |        | Yes              |
| EC_CMD_GET_GPU_SERIAL              | 0x3E1D |       |        | Yes              |
| EC_CMD_GET_GPU_PCIE                | 0x3E1E |       |        | Yes              |
| EC_CMD_PROGRAM_GPU_EEPROM          | 0x3E1F |       |        | Yes              |
| EC_CMD_FP_CONTROL                  | 0x3E20 |       |        | No               |
| EC_CMD_GET_CUTOFF_STATUS           | 0x3E21 |       |        | No               |
| EC_CMD_GET_AP_THROTTLE_STATUS      | 0x3E22 | Yes   | Yes    | Yes              |
| EC_CMD_GET_PD_PORT_STATE           | 0x3E23 |       |        | Yes              |
| EC_CMD_BATTERY_EXTENDER            | 0x3E24 |       |        | No               |
| EC_CMD_WAKE_ON_LAN                 | 0x3E25 |       |        | No               |
