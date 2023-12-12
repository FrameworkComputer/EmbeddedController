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
`CONFIG_PLATFORM_EC_ALS`                         | n       | [ALS]
`CONFIG_PLATFORM_EC_DYNAMIC_MOTION_SENSOR_COUNT` | n       | [DYNAMIC SENSOR COUNT]
`CONFIG_PLATFORM_EC_LID_ANGLE`                   | n       | [LID ANGLE]
`CONFIG_PLATFORM_EC_CONSOLE_CMD_ACCELS`          | n       | [ACCELS CMD]
`CONFIG_PLATFORM_EC_ACCEL_SPOOF_MODE`            | n       | [ACCEL SPOOF MODE]
`CONFIG_PLATFORM_EC_MAX_SENSOR_FREQ_MILLIHZ`     | n       | [MAX SENSOR FREQUENCY]

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

Initiating the devicetree nodes for motionsense requires a bit of internal
knowledge of the drivers'
[bindings](https://source.chromium.org/chromium/chromiumos/platform/ec/+/HEAD:zephyr/dts/bindings/motionsense/).

### Sensor mutex

Some drivers, such as the BMI260, which combine 2 sensors (accelerometer and
gyroscope) will require a mutex to be defined. The individual nodes in the
devicetree will accept a `phandle` pointing to the mutex
[here](https://source.chromium.org/chromium/chromiumos/platform/ec/+/HEAD:zephyr/dts/bindings/motionsense/motionsense-sensor-base.yaml;l=37;drc=67e0b58c17177858595001baa1aa607b54b18d11).
It is important for both nodes of these sensors to point to the same mutex (
see example below).

### Location and orientation

The location of the sensor is dictated in an enum property
[location](https://source.chromium.org/chromium/chromiumos/platform/ec/+/HEAD:zephyr/dts/bindings/motionsense/motionsense-sensor-base.yaml;l=29;drc=67e0b58c17177858595001baa1aa607b54b18d11).
It is important to note that sensors which are combined in a single chip (such
as the BMI260) should have the same value for both nodes.
This property allows the distinction between sensors location at the following:

* "base" - the base of the laptop (where the keyboard is).
* "lid" - the screen surface of the laptop.
* "camera" - an explicit location for the camera.

In addition to the location, each sensor node should also include a reference
to a [rotation matrix](https://source.chromium.org/chromium/chromiumos/platform/ec/+/HEAD:zephyr/dts/bindings/motionsense/motionsense-sensor-base.yaml;l=45;drc=67e0b58c17177858595001baa1aa607b54b18d11).
This will be used to orient the sensor data according to the standardised screen
coordinates.

### Internal sensor data

Internal sensor data nodes can be found [here](https://source.chromium.org/chromium/chromiumos/platform/ec/+/HEAD:zephyr/dts/bindings/motionsense/drvdata/).
These are required, one per physical sensor, in order to instantiate the
internal data structures. It is important to check the required properties of
each binding to ensure proper instantiation.

### Motion sensor nodes

All motion sensor nodes (accel, gyro, and mag) should be declared inside the
`motionsense-sensor` node. This will in-turn create the `motion_sensors` array
required for the sensor logic.

### Additional sensor meta-data

Additional information about sensors can be provided by the
[motionsense-sensor-info](https://source.chromium.org/chromium/chromiumos/platform/ec/+/HEAD:zephyr/dts/bindings/motionsense/cros-ec,motionsense-sensor-info.yaml).
This node provides information about the ALS (ambient light sensor) list, the
various interrupts, and sensors which are placed in force mode (motionsense will
directly query these sensors instead of waiting for an interrupt).

## Board Specific Code

Motionsense requires no board specific code.

## Threads

When enabled, the `motion_sense_task` will be created. The task's stack size can
be set using `CONFIG_TASK_MOTIONSENSE_STACK_SIZE`, but the priority is fixed.
Additional information about the task priority can be found in
[shimmed_task_id.h](https://source.chromium.org/chromium/chromiumos/platform/ec/+/HEAD:zephyr/shim/include/shimmed_task_id.h).

## Testing and Debugging

Properly debugging the sensor framework generally involves one of the following:

* If the device is a convertible (laptop/tablet) flip the screen over and make
  sure that screen rotations work.
* In the EC console test the following commands:
  * `accelrange id [data [roundup]]` where the `id` is the sensor number. If
    `data` is not provided, then this command will perform a read and print the
    range of the motion sensor. Otherwise, it will set the range to the nearest
    `data` value depending on `roundup` being either `0` or `1`.
  * `accelres id [data [roundup]]` where the `id` is the sensor number. If
    `data` is not provided, then this command will perform a read and print the
    resolution of the motion sensor. Otherwise, it will set the resolution to
    the nearest `data` value depending on `roundup` being either `0` or `1`.
  * `accelrate id [data [roundup]]` where the `id` is the sensor number. If
    `data` is not provided, then this command will perform a read and print the
    ODR of the motion sensor. Otherwise, it will set the ODR to the nearest
    `data` value depending on `roundup` being either `0` or `1`.
  * `accelread id [n]` where the `id` is the sensor number. If `n` is provided,
    the sensor will be read `n` times. Otherwise, only a single read operation
    will take place.
* From the kernel root shell, test the following commands:
  * `ectool motionsense` to print all the motion data.
  * `ectool lid_angle` to verify that the rotation matrices are set correctly
    for the lid and base accelerometers.
  * See [ectool] for additional commands or run `ectool help motionsense`.

## Example

### Create the mutex

The mutexes are created as child nodes of the `cros-ec,motionsense-mutex`
compatible string.

```
motionsense-mutex {
  compatible = "cros-ec,motionsense-mutex";
  lid_mutex: lid-mutex {
  };
};
```

### Create the rotation reference

The rotation references are created as child nodes of the
`cros-ec,motionsense-rotation-ref` compatible string.

```
motionsense-rotation-ref {
  compatible = "cros-ec,motionsense-rotation-ref";
  lid_rot_ref: lid-rotation-ref {
    mat33 = <1 0 0
             0 1 0
             0 0 1>;
  };
};
```

### Create the sensor data

Data structures are iterated by their compatible string and do not need to be
clustered under a parent node (though they usually are).

```
bmi260_data: bmi260-drv-data {
  compatible = "cros-ec,drvdata-bmi260";
  status = "okay";
};
```

### Create the sensor list

The sensor list is created from the child nodes of the `motionsense-sensor`
node.

```
motionsense-sensor {
  lid_accel: lid-accel {
    compatible = "cros-ec,bmi260-accel";
    status = "okay";
    /* Set the active mask so this sensor will be on in both S0 and S3 */
    active-mask = "SENSOR_ACTIVE_S0_S3";
    /* Set the location of the sensor */
    location = "MOTIONSENSE_LOC_LID";
    /* Set the mutex (must be the same as the gyro) */
    mutex = <&lid_mutex>;
    /* Set the rotation reference */
    rot-standard-ref = <&lid_rot_ref>;
    /* Set the data for the driver (must be the same as the gyro) */
    drv-data = <&bmi260_data>;
  };
  lid_gyro: lid-gyro {
    compatible = "cros-ec,bmi260-gyro";
    status = "okay";
    active-mask = "SENSOR_ACTIVE_S0_S3";
    location = "MOTIONSENSE_LOC_LID";
    mutex = <&lid_mutex>;
    rot-standard-ref = <&lid_rot_ref>;
    drv-data = <&bmi260_data>;
  };
};
```

### Configuring the sensor

Some additional configurations can be provided to the first node of a sensor.
For example, in the above, the `lid_accel` can include a `configs` section which
will allow the EC to automatically re-configure the sensor on various power
levels. For example, the following will enable the lid accelerometer in both
`S0` and `S3` and set the `ODR` to 10,000mHz or the nearest higher sample rate.

```
lid_accel: lid-accel {
  ...
  configs {
    compatible = "cros-ec,motionsense-sensor-config";
    ec-s0 {
      odr = <(10000 | ROUND_UP_FLAG)>;
    };
    ec-s3 {
      odr = <(10000 | ROUND_UP_FLAG)>;
    };
  };
};
```

[MOTIONSENSE]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.motionsense;?q="config%20PLATFORM_EC_MOTIONSENSE"&ss=chromiumos
[ACCEL FIFO]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.motionsense;?q="config%20PLATFORM_EC_ACCEL_FIFO"&ss=chromiumos
[TIGHT TIMESTAMPS]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.motionsense;?q="config%20PLATFORM_EC_SENSOR_TIGHT_TIMESTAMPS"&ss=chromiumos
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
[MAX SENSOR FREQUENCY ]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.motionsense;?q="config%20PLATFORM_EC_MAX_SENSOR_FREQ_MILLIHZ"&ss=chromiumos
[ectool]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/util/ectool.c;?q=function:ms_help&ss=chromiumos
