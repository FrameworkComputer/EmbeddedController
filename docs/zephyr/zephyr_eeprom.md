# Zephyr CBI EEPROM Configuration.

[TOC]

## Overview

Zephyr `EEPROM` [`CBI Configuration`]

Note: Since the `EEPROM` uses an [`I2C`] interface that must be
configured and working before enabling [`CBI`].

## Kconfig Options

The CrOS Board Information [`CBI`] feature is enabled with the
CONFIG_PLATFORM_EC_CBI_EEPROM Kconfig, as defined in the [`CBI Configuration`].

The specific `EEPROM` also needs to be enabled. For example, the Atmel AT24
would need the following enabled.

Kconfig Option                  | Enabled state | Documentation
:------------------------------ | :-----------: | :------------
`CONFIG_EEPROM`                 | y             | Enabled EEPROM
`CONFIG_EEPROM_AT24`            | y             | Enable Atmel AT24

Device tree is used to define and specify the `EEPROM` device.

## Devicetree Nodes

The `EEPROM` device tree nodes are defined for each type of device
YAML bindings that are specific to that particular `EEPROM`.  The standard
fashion of defining that `EEPROM` is used with one exception, the `EEPROM`
node must have the nodelabel `cbi_eeprom`.  Including the `cbi_eeprom`
nodelabel in the Device Tree will include the cbi_eeprom driver.

An example definition of the Atmel AT24 is:
```
    &i2c0_0 {
        label = "I2C_EEPROM";
        clock-frequency = <I2C_BITRATE_FAST>;

        cbi_eeprom: eeprom@50 {
            compatible = "atmel,at24";
            reg = <0x50>;
            label = "EEPROM_CBI";
            size = <2048>;
            pagesize = <16>;
            address-width = <8>;
            timeout = <5>;
        };
    };
```

Configuring the [`CBI`] Write Protect Signal is also necessary.
The current requirement is that you define an alias node called
"gpio-cbi-wp" that points to the GPIO used as the write protect.

Note that the "wp-gpios" property should not be used.
"wp-gpios" - Specifies the GPIO output signal from the SoC connected
to the `EEPROM` write protect pin.  Zephyr uses this to automatically
control write protection during write and erase operations.
On Chromebooks, the write protect signal is controlled by the Google
Security Chip.  gpio-cbi-wb specifies the GPIO input signal to the SoC,
used by the EC application to monitor the write protect state.

An example of configuring this is:
```
/ {
    aliases {
        gpio-cbi-wp = &gpio_cbi_wp;
    };
    named-gpios {
        gpio_cbi_wp: ec_cbi_wp {
            gpios = <&gpio8 1 GPIO_OUT_LOW>;
        };
    };
};
```

## Threads

No threads used in this feature.

## Testing and Debugging

Also the [`I2C bus scan`] can be used to verify the EEPROM device can be accessed.


[`CBI`]: https://chromium.googlesource.com/chromiumos/docs/+/HEAD/design_docs/cros_board_info.md
[`CBI Configuration`]: ./zephyr_cbi.md
[`I2C bus scan`]: ./zephyr_i2c.md#Shell-Command_i2c
