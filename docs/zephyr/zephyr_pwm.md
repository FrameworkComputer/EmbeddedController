# Zephyr EC PWM Configuration

[TOC]

## Overview

[PWM] provides support for PWM setup and control on the platform.

## Kconfig Options

Kconfig Option | Default | Documentation
:------------- | :------ | :------------
`CONFIG_PWM` | n | [PWM (Pulse Width Modulation) Drivers]
`CONFIG_PWM_<platform>` | n | Platform specific PWM driver

Kconfig sub-option | Default | Documentation
:----------------- | :------ | :------------
`CONFIG_PLATFORM_EC_PWM_INIT_PRIORITY` | 51 | [Init priority of the PWM module]

## Devicetree Nodes

PWM channels are configured by enabling the PWM subsystem in the project
`prj.conf`, setting a phandle reference to higher level driver `pwms` property,
and enabling and configuring the corresponding PWM controller device node.

For example for a keyboard backlight device:

```
kblight {
        compatible = "cros-ec,kblight-pwm";
        pwms = <&pwm3 0 PWM_HZ(2400) PWM_POLARITY_NORMAL>;
};
```

Property | Description | Settings
:------- | :---------- | :-------
`pwms` | PWM phandle, identifies the controller (X), channel (Y) and flags. | `<&pwmX Y flags>`
`frequency` | PWM frequency, in Hz | `integer (32 bit unsigned)`

The `flags` cell of `pwms` defines the PWM signal properties, valid options are
listed in the [dt-bindings/pwm/pwm.h], commonly used ones are
`PWM_POLARITY_NORMAL` and `PWM_POLARITY_INVERTED`.

Any used platform PWM device has to be explicitly enabled and configured, for example:

```
&pwm3 {
	status = "okay";
	clock-bus = "NPCX_CLOCK_BUS_LFCLK"; /* Keep active during low power mode. */
};
```

The device node may include any platform specific property, see the
corresponding bindings files for more details ([nuvoton,npcx-pwm],
[ite,it8xxx2-pwm]).

## Board Specific Code

None required.

## Threads

PWM support does not enable any thread.

## Testing and Debugging

### Shell Command

Zephyr defines a `pwm` shell command that can be used to change the current PWM
duty cycle and period.

The command is enabled by setting `CONFIG_PWM_SHELL=y` ([CONFIG_PWM_SHELL]) in the project `prj.conf` file.

Command | Description | Usage
:------ | :---------- | :----
`pwm` | PWM shell commands | `pwm <cycles or usec or nsec> <device> <pwm> <period> <pulse> [flags]`

Parameter | Description
:-------- | :----------
`cycles/usec/nsec` | The unit of period and pulse (cycles, microseconds or nanoseconds).
`device` | The PWM device label (for example `PWM_1`).
`pwm` | The PWM channel number.
`period` | Requested period of the PWM signal.
`pulse` | Requested active cycle duration of the PWM signal.
`flags` | PWM flags (for example `1` for inverted polarity).

For example to set the keyboard backlight (connected to PWM3) to 90%  at 250Hz on Volteer:

`$ pwm usec PWM_3 0 250 225`

## Features using PWM directly

### Keyboard backlight

Kconfig Option | Default | Documentation
:------------- | :------ | :------------
`PLATFORM_EC_PWM_KBLIGHT` | n | [PWM keyboard backlight]

This requires defining a node with `compatible = "cros-ec,kblight-pwm"`,
normally in a separate overlay `keyboard.dts` file, for example on the Volteer
reference board:

![PWM Example]

The keyboard backlight line is connected to `PWM3` channel 0 (there's only one
channel per PWM on NPCX), normal polarity:

```
/ {
        kblight {
                compatible = "cros-ec,kblight-pwm";
                pwms = <&pwm3 0 PWM_HZ(2400) PWM_POLARITY_NORMAL>;
        };
};

&pwm3 {
        status = "okay";
};
```

[PWM]: ../ec_terms.md#pwm
[PWM (Pulse Width Modulation) Drivers]: https://docs.zephyrproject.org/latest/kconfig.html#CONFIG_PWM
[PWM (Pulse Width Modulation) module]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig?q=%22config%20PLATFORM_EC_PWM%22
[Init priority of the PWM module]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.init_priority?q=%22config%20PLATFORM_EC_PWM_INIT_PRIORITY%22
[CONFIG_PWM_SHELL]: https://docs.zephyrproject.org/latest/kconfig.html#CONFIG_PWM_SHELL
[PWM display backlight]:https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig?q=%22config%20PLATFORM_EC_PWM_DISPLIGHT%22
[PWM keyboard backlight]:https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig?q=%22config%20PLATFORM_EC_PWM_KBLIGHT%22
[dt-bindings/pwm/pwm.h]: https://github.com/zephyrproject-rtos/zephyr/blob/main/include/dt-bindings/pwm/pwm.h
[PWM Example]: pwm_schematic.png
[nuvoton,npcx-pwm]: https://github.com/zephyrproject-rtos/zephyr/blob/main/dts/bindings/pwm/nuvoton%2Cnpcx-pwm.yaml
[ite,it8xxx2-pwm]: https://github.com/zephyrproject-rtos/zephyr/blob/main/dts/bindings/pwm/ite%2Cit8xxx2-pwm.yaml
