# Zephyr Sensor Devices

[TOC]

## Overview

Zephyr provides a way to enable the legacy cros-ec sensor drivers. This is done
through both enabling Kconfig options and adding nodes to the devicetree.

## Kconfig Options

Kconfig Option                          | Default | Documentation
:-------------------------------------- | :-----: | :------------
`CONFIG_PLATFORM_EC_ACCELGYRO_BMI160`   | n       | [BMI160]
`CONFIG_PLATFORM_EC_ACCELGYRO_BMI260`   | n       | [BMI260]
`CONFIG_PLATFORM_EC_ACCELGYRO_BMI3XX`   | n       | [BMI3XX]
`CONFIG_PLATFORM_EC_ACCELGYRO_ICM426XX` | n       | [ICM426XX]
`CONFIG_PLATFORM_EC_ACCELGYRO_ICM42607` | n       | [ICM42607]
`CONFIG_PLATFORM_EC_ACCELGYRO_LSM6DSO`  | n       | [LSM6DSO]
`CONFIG_PLATFORM_EC_ACCEL_BMA255`       | n       | [BMA255]
`CONFIG_PLATFORM_EC_ACCEL_BMA4XX`       | n       | [BMA4XX]
`CONFIG_PLATFORM_EC_ACCEL_KX022`        | n       | [KX022]
`CONFIG_PLATFORM_EC_ACCEL_LIS2DW12`     | n       | [LIS2DW12]
`CONFIG_PLATFORM_EC_ALS_TCS3400`        | n       | [TCS3400]
`CONFIG_PLATFORM_EC_ALS_VEML3328`       | n       | [VEML3328]

### CONFIG_PLATFORM_EC_ACCELGYRO_BMI_COMM choice

The following choice is available only when one of the
`CONFIG_PLATFORM_EC_ACCELGYRO_BMI*` configs are selected.

Kconfig choice                              | Documentation
:------------------------------------------ | :------------
`CONFIG_PLATFORM_EC_ACCELGYRO_BMI_COMM_SPI` | [BMI COMM SPI]
`CONFIG_PLATFORM_EC_ACCELGYRO_BMI_COMM_I2C` | [BMI COMM I2C]

### CONFIG_PLATFORM_EC_ACCELGYRO_ICM_COMM choice

The following choice is available only when one of the
`CONFIG_PLATFORM_EC_ACCELGYRO_ICM*` configs are selected.

Kconfig choice                              | Documentation
:------------------------------------------ | :------------
`CONFIG_PLATFORM_EC_ACCELGYRO_ICM_COMM_SPI` | [ICM COMM SPI]
`CONFIG_PLATFORM_EC_ACCELGYRO_ICM_COMM_I2C` | [ICM COMM I2C]

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

[BMI160]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.sensor_devices?q=%22config%20PLATFORM_EC_ACCELGYRO_BMI160%22&ss=chromiumos
[BMI260]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.sensor_devices?q=%22config%20PLATFORM_EC_ACCELGYRO_BMI260%22&ss=chromiumos
[BMI3XX]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.sensor_devices?q=%22config%20PLATFORM_EC_ACCELGYRO_BMI3XX%22&ss=chromiumos
[BMI COMM SPI]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.accelgyro_bmi?q=%22config%20PLATFORM_EC_ACCELGYRO_BMI_COMM_SPI%22&ss=chromiumos
[BMI COMM I2C]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.accelgyro_bmi?q=%22config%20PLATFORM_EC_ACCELGYRO_BMI_COMM_I2C%22&ss=chromiumos

[ICM426XX]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.sensor_devices?q=%22config%20PLATFORM_EC_ACCELGYRO_ICM426XX%22&ss=chromiumos
[ICM42607]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.sensor_devices?q=%22config%20PLATFORM_EC_ACCELGYRO_ICM42607%22&ss=chromiumos
[ICM COMM SPI]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.accelgyro_bmi?q=%22config%20PLATFORM_EC_ACCELGYRO_ICM_COMM_SPI%22&ss=chromiumos
[ICM COMM I2C]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.accelgyro_bmi?q=%22config%20PLATFORM_EC_ACCELGYRO_ICM_COMM_I2C%22&ss=chromiumos

[LSM6DSO]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.sensor_devices?q=%22config%20PLATFORM_EC_ACCELGYRO_LSM6DSO%22&ss=chromiumos

[BMA255]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.sensor_devices?q=%22config%20PLATFORM_EC_ACCEL_BMA255%22&ss=chromiumos
[BMA4XX]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.sensor_devices?q=%22config%20PLATFORM_EC_ACCEL_BMA4XX%22&ss=chromiumos

[KX022]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.sensor_devices?q=%22config%20PLATFORM_EC_ACCEL_KX022%22&ss=chromiumos

[LIS2DW12]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.sensor_devices?q=%22config%20PLATFORM_EC_ACCEL_LIS2DW12%22&ss=chromiumos

[TCS3400]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.sensor_devices?q=%22config%20PLATFORM_EC_ALS_TCS3400%22&ss=chromiumos
