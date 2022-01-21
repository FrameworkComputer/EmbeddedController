# Zephyr EC MotionSense

[TOC]

## Overview

Zephyr's wrapping of the EC motionsense provides a quick configuration of the
sensor framework via Zephyr's device tree.

## Kconfig Options

The following are the various Kconfig options for the motionsense logic. Every
config option depends on having `CONFIG_PLATFORM_EC_MOTIONSENSE=y`.

Kconfig Option                                   | Default | Documentation
:----------------------------------------------- | :-----: | :------------
`CONFIG_PLATFORM_EC_MOTIONSENSE`                 | n       | [MOTIONSENSE]
`CONFIG_PLATFORM_EC_ACCEL_FIFO`                  | n       | [ACCEL FIFO]
`CONFIG_PLATFORM_EC_SENSOR_TIGHT_TIMESTAMPS`     | n       | [TIGHT TIMESTAMPS]
`CONFIG_PLATFORM_EC_ACCEL_INTERRUPTS`            | n       | [ACCEL INTERRUPTS]
`CONFIG_PLATFORM_EC_ALS`                         | n       | [ALS]
`CONFIG_PLATFORM_EC_DYNAMIC_MOTION_SENSOR_COUNT` | n       | [DYNAMIC SENSOR COUNT]
`CONFIG_PLATFORM_EC_LID_ANGLE`                   | n       | [LID ANGLE]
`CONFIG_PLATFORM_EC_CONSOLE_CMD_ACCELS`          | n       | [ACCELS CMD]
`CONFIG_PLATFORM_EC_ACCEL_SPOOF_MODE`            | n       | [ACCEL SPOOF MODE]

Additional Kconfig options are available at
[Kconfig.sensor_devices](./zephyr_sensor_devices.md).

### CONFIG_PLATFORM_EC_ACCEL_FIFO sub configs

The following options are available only when `CONFIG_PLATFORM_EC_ACCEL_FIFO=y`.

Kconfig sub-option                     | Default | Documentation
:------------------------------------- | :-----: | :------------
`CONFIG_PLATFORM_EC_ACCEL_FIFO_SIZE`   | 256     | [ACCEL FIFO SIZE]
`CONFIG_PLATFORM_EC_ACCEL_FIFO_THRES`  | 85      | [ACCEL FIFO THRES]

### CONFIG_PLATFORM_EC_LID_ANGLE sub configs

The following options are available only when `CONFIG_PLATFORM_EC_LID_ANGLE=y`.

Kconfig sub-option                     | Default | Documentation
:------------------------------------- | :-----: | :------------
`CONFIG_PLATFORM_EC_LID_ANGLE_UPDATE`  | n       | [LID ANGLE UPDATE]
`CONFIG_PLATFORM_EC_TABLET_MODE`       | n       | [TABLET MODE]

### CONFIG_PLATFORM_EC_TABLET_MODE sub configs

The following options are available only when `CONFIG_PLATFORM_EC_TABLET_MODE=y`.

Kconfig sub-option                       | Default | Documentation
:--------------------------------------- | :-----: | :------------
`CONFIG_PLATFORM_EC_TABLET_MODE_SWITCH`  | n       | [TABLET MODE SWITCH]
`CONFIG_PLATFORM_EC_GMR_TABLET_MODE`     | n       | [GMR TABLET MODE]

### CONFIG_PLATFORM_EC_CONSOLE_CMD_ACCELS sub configs

The following options are available only when `CONFIG_PLATFORM_EC_CONSOLE_CMD_ACCELS=y`.

Kconfig sub-option                           | Default | Documentation
:------------------------------------------- | :-----: | :------------
`CONFIG_PLATFORM_EC_CONSOLE_CMD_ACCEL_INFO`  | n       | [ACCEL INFO CMD]

### CONFIG_PLATFORM_EC_ACCEL_SPOOF_MODE sub configs

The following options are available only when `CONFIG_PLATFORM_EC_ACCEL_SPOOF_MODE=y`.

Kconfig sub-option                           | Default | Documentation
:------------------------------------------- | :-----: | :------------
`CONFIG_PLATFORM_EC_CONSOLE_CMD_ACCEL_SPOOF` | n       | [ACCEL SPOOF CMD]

## Devicetree Nodes

*Detail the devicetree nodes that configure the feature.*

*Note - avoid documenting node properties here.  Point to the relevant `.yaml`
file instead, which contains the authoritative definition.*

## Board Specific Code

*Document any board specific routines that a user must create to successfully
compile and run. For many features, this can section can be empty.*

## Threads

*Document any threads enabled by this feature.*

## Testing and Debugging

*Provide any tips for testing and debugging the EC feature.*

## Example

*Provide code snippets from a working board to walk the user through
all code that must be created to enable this feature.*

<!--
The following demonstrates linking to a code search result for a Kconfig option.
Reference this link in your text by matching the text in brackets exactly.
-->
[MOTIONSENSE]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.motionsense;?q="config%20PLATFORM_EC_MOTIONSENSE"&ss=chromiumos
[ACCEL FIFO]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.motionsense;?q="config%20PLATFORM_EC_ACCEL_FIFO"&ss=chromiumos
[TIGHT TIMESTAMPS]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.motionsense;?q="config%20PLATFORM_EC_SENSOR_TIGHT_TIMESTAMPS"&ss=chromiumos
[ACCEL INTERRUPTS]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.motionsense;?q="config%20PLATFORM_EC_ACCEL_INTERRUPTS"&ss=chromiumos
[ALS]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.motionsense;?q="config%20PLATFORM_EC_ALS"&ss=chromiumos
[DYNAMIC SENSOR COUNT]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.motionsense;?q="config%20PLATFORM_EC_DYNAMIC_MOTION_SENSOR_COUNT"&ss=chromiumos
[LID ANGLE]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.motionsense;?q="config%20PLATFORM_EC_LID_ANGLE"&ss=chromiumos
[ACCELS CMD]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.motionsense;?q="config%20PLATFORM_EC_CONSOLE_CMD_ACCELS"&ss=chromiumos
[ACCEL SPOOF MODE]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.motionsense;?q="config%20PLATFORM_EC_ACCEL_SPOOF_MODE"&ss=chromiumos
[ACCEL FIFO SIZE]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.motionsense;?q="config%20PLATFORM_EC_ACCEL_FIFO_SIZE"&ss=chromiumos
[ACCEL FIFO THRES]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.motionsense;?q="config%20PLATFORM_EC_ACCEL_FIFO_THRES"&ss=chromiumos
[ALS COUNT]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.motionsense;?q="config%20PLATFORM_EC_ALS_COUNT"&ss=chromiumos
[LID ANGLE UPDATE]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.motionsense;?q="config%20PLATFORM_EC_LID_ANGLE_UPDATE"&ss=chromiumos
[TABLET MODE]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.motionsense;?q="config%20PLATFORM_EC_TABLET_MODE"&ss=chromiumos
[TABLET MODE SWITCH]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.motionsense;?q="config%20PLATFORM_EC_TABLET_MODE_SWITCH"&ss=chromiumos
[GMR TABLET MODE]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.motionsense;?q="config%20PLATFORM_EC_GMR_TABLET_MODE"&ss=chromiumos
[ACCEL INFO CMD]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.motionsense;?q="config%20PLATFORM_EC_CONSOLE_CMD_ACCEL_INFO"&ss=chromiumos
[ACCEL SPOOF CMD]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.motionsense;?q="config%20PLATFORM_EC_CONSOLE_CMD_ACCEL_SPOOF"&ss=chromiumos
