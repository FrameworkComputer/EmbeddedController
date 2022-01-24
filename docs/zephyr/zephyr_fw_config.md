# Zephyr FW_CONFIG configuration and use.

[TOC]

## Overview

Zephyr CBI FW_CONFIG configuration

## Kconfig Options

The CBI [Cross Board Info](https://chromium.googlesource.com/chromiumos/docs/+/HEAD/design_docs/cros_board_info.md)
contains a variety of fields that the EC retrieves from an EEPROM.

The CBI feature is enabled using:

Kconfig Option                   | Default | Documentation
:--------------------------------| :-----: | :------------
`CONFIG_PLATFORM_EC_CBI_EEPROM`   | n       | [zephyr/Kconfig](../zephyr/Kconfig)

One of the CBI elements is the
[`FW_CONFIG`](https://chromium.googlesource.com/chromiumos/docs/+/HEAD/design_docs/firmware_config.md)
field.
This config is used at run time to select different hardware options or behaviours, and
is defined generally on a board by board basis.
The `FW_CONFIG` block is limited to 32 bits. The 32 bits are divided into individual
fields of varying sizes. Each field has defined values that may be set to control
the behaviour according to the definition for that board.

Device tree is used to define and specify the field sizes and values.

## Devicetree Nodes

The `FW_CONFIG` device tree nodes are defined via the
[cros-ec-cbi-fw-config](../zephyr/dts/bindings/cbi/cros-ec-cbi-fw-config.yaml)
and
[cros-ec-cbi-fw-config-value](../zephyr/dts/bindings/cbi/cros-ec-cbi-fw-config-value.yaml)
YAML bindings.

The `cros-ec-cbi-fw-config` bindings define the name, starting bit and size of each field.
The `cros-ec-cbi-fw-config-value` bindings allow names/values to be defined for each
value that may be stored in the field.
One of the values may be designated as the default, which is used if
the CBI data cannot be accessed.

Spare CBI FW_CONFIG fields are always initialised as zeros, so that
future fields will have a guaranteed value, so typically a zero
value is used as a default, indicating the default field.

An example definition is:
```
puff-fw-config {
   compatible = "cbi-fw-config";
   bj-power {
       enum-name = "FW_BJ_POWER";
       start = <0>;
       size = <4>;
       p65w {
           enum-name = "BJ_POWER_P65";
           compatible = "cbi-fw-config-value";
           value = <0>;
       };
       p90w {
           enum-name = "BJ_POWER_P90";
           compatible = "cbi-fw-config-value";
           value = <1>;
           default;
       };
    };
    no-usb-port-4 {
        enum-name = "FW_USB_PORT_4";
        start = <4>;
        size = <1>;
        port-4-present {
            enum-name = "USB_PORT_4_PRESENT";
            compatible = "cbi-fw-config-value";
            value = <1>;
        };
    };
};
```

The device tree will generate a series of
enum values and field names that can used
to read the values (via the CBI driver).

## Board Specific Code

To access the generated enums and names, the CBI driver
should be used to access the CBI API to retrieve selected fields,
and then the defined enums used e.g:

```
#include <drivers/cros_cbi.h>

    int get_power_watts()
    {
        const struct device *dev;
	int ret;
	uint32_t val;

	dev = device_get_binding(CROS_CBI_LABEL);
	if (dev == null)
	    return -1;

	ret = cros_cbi_get_fw_config(dev, FW_BJ_POWER, &val);
	if (ret < 0)
	    return -1;

	if (val == BJ_POWER_P65)
	    return 65;
	if (val == BJ_POWER_P90)
	    return 90;
	return -1;
    }
```

## Threads

No threads used in this feature.

## Testing and Debugging

There are unit tests.
