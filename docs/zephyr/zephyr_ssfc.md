# Zephyr CBI SSFC Configuration and Use.

[TOC]

## Overview

Zephyr [`SSFC`] [`CBI Configuration`]

## Kconfig Options

The CrOS Board Information [`CBI`] feature is enabled with the
CONFIG_PLATFORM_EC_CBI_EEPROM Kconfig, as defined in the [`CBI Configuration`].

One of the [`CBI`] elements is the Second Source Factory Cache [`SSFC`] field.
This config is used at run time to select different hardware options or behaviours, and
is defined generally on a board by board basis.  The [`SSFC`] describes later decisions
for a board to indicate alternate second sourced hardware stuffing
which can be used by the EC to know which drivers to load.

The [`SSFC`] block is limited to 32 bits. The 32 bits are divided into individual
fields of varying sizes. Each field has defined values that may be set to control
the behaviour according to the definition for that board.

Device tree is used to define and specify the field sizes and values.

## Devicetree Nodes

The [`SSFC`] device tree nodes are defined via the [`named-cbi-ssfc`] and
[`named-cbi-ssfc-value`] YAML bindings.

The [`named-cbi-ssfc`] bindings define the name and size of each field.
The [`named-cbi-ssfc-value`] bindings allow names/values to be defined for each
value that may be stored in the field.
One of the values may be designated as the default, which is used if
the [`CBI`] data cannot be accessed.

Spare [`CBI`] [`SSFC`] fields are always initialised as zeros, so that
future fields will have a guaranteed value, so typically a zero
value is used as a default, indicating the default field.

An example definition is:
```
cbi-ssfc {
    compatible = "named-cbi-ssfc";
    base_sensor {
        enum-name = "BASE_SENSOR";
        size = <3>;
        base_sensor_0: bmi160 {
            compatible = "named-cbi-ssfc-value";
            status = "okay";
            value = <1>;
        };
    };
    lid_sensor {
        enum-name = "LID_SENSOR";
        size = <3>;
        lid_sensor_0: bma255 {
            compatible = "named-cbi-ssfc-value";
            status = "okay";
            value = <1>;
        };
    };
    lightbar {
        enum-name = "LIGHTBAR";
        size = <2>;
        lightbar_0: 10_led {
            compatible = "named-cbi-ssfc-value";
            status = "okay";
            value = <1>;
        };
    };
};
```

## Board Specific Code

To access the presence of an [`SSFC`] component, the [`CBI`] driver
should be used to access the [`CBI`] API to check for an [`SSFC`] match e.g:

```
#include "cros_cbi.h"

    bool is_base_sensor_bmi160(void)
    {
        return cros_cbi_ssfc_check_match(
            CBI_SSFC_VALUE_ID(DT_NODELABEL(base_sensor_0)));
    }
```

## Threads

No threads used in this feature.

## Testing and Debugging

The [`ectool cbi`] command can be used to read and set the [`SSFC`].


[`CBI`]: https://chromium.googlesource.com/chromiumos/docs/+/HEAD/design_docs/cros_board_info.md
[`CBI Configuration`]: ./zephyr_cbi.md
[`ectool cbi`]: ./zephyr_cbi.md#testing-and-debugging
[`named-cbi-ssfc`]: ../../zephyr/dts/bindings/cbi/named-cbi-fw-config.yaml
[`named-cbi-ssfc-value`]: ../../zephyr/dts/bindings/cbi/named-cbi-fw-config-value.yaml
[`SSFC`]: https://chromium.googlesource.com/chromiumos/docs/+/HEAD/design_docs/firmware_config.md
