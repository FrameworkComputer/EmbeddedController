# Zephyr EC Keyboard Configuration

[TOC]

## Overview

This provides an overview of the Keyboard configuration and options.

## Reference implementations

A full configuration of a keyboard device on NPCX is available in
[keyboard-rex] and on ITE on [keyboard-brox]. For a reference using a full 18x8
matrix see [keyboard-jubilant].

## Kconfig Options

Kconfig Option | Default | Documentation
:------------- | :------ | :------------
`CONFIG_INPUT` | n | Enables the input subsystem, required by the keyboard driver
`CONFIG_PM_DEVICE_RUNTIME` | n | Enables the runtime PM APIs, required by the keyboard driver to work properly
`CONFIG_CROS_EC_KEYBOARD_INPUT` | n | Enable the input subsystem based keyboard driver
`CONFIG_INPUT_SHELL` | n | General option to enable the input shell commands
`CONFIG_INPUT_SHELL_KBD_MATRIX_STATE` | n | Enable the keyboard matrix debug shell command

## Devicetree Nodes

The keyboard device is configured by extending the `kbd` node for a specific
program, typically in a dedicated `keyboard.dtsi` file. A basic configuration
should contain the list of pins and matrix size in row and columns.

```
&kbd {
        status = "okay";
        pinctrl-0 = <
                &ksi0_default
                ...
                &kso12_default
        >;
        pinctrl-1 = <
                &ksi0_sleep
                ...
                &kso12_sleep
        >;
        pinctrl-names = "default", "sleep";
        row-size = <8>;
        col-size = <13>;
};
```

Few extra properties are available to fine tune the keyboard scanning algorithm

Property | Description | Settings
:------- | :---------- | :-------
`poll-period-ms` |  Defines the poll period in milliseconds between between matrix scans. | `int` (default 5)
`poll-timeout-ms` | How long to wait before going from polling back to idle state. | `int` (default 100)
`debounce-down-ms` | Debouncing time for a key press event. | `int` (default 10)
`debounce-up-ms` | Debouncing time for a key release event. | `int` (default 20)
`settle-time-us` | Delay between setting column output and reading the row values. | `int` (default 50)
`actual-key-mask` | Keyboard scanning mask. For each keyboard column, specify which keyboard rows actually exist. | `array of uint8` (default unset)
`no-ghostkey-check` | Ignore the ghost key checking in the driver, for devices with anti ghosting diodes. | `boolean` (default n)

Typically `settle-time-us` is set to 80us to account for signal propagation
delay in the col2 data line.

Other features are configured using devicetree nodes, defined as child nodes of
the keyboard device:

### KSO2

The KSO2 signal typically has its own dedicated handling code, the pin is
defined with a special `cros-ec,col-gpio` node, for example:

```
&kbd {
        ...
        kso-gpio {
                compatible = "cros-ec,col-gpio";
                col-num = <2>;
                col-gpios = <&gpioksol 2 GPIO_ACTIVE_HIGH>;
        };
        ...
};
```

The `col-num` property specifies which column to handle, and `col-gpios` what
GPIO to use.

### Boot and runtime keys

The boot and runtime key row and column keys are configured with a
`cros-ec,boot-keys` node, for example:

```
&kbd {
        ...
        boot-keys {
                compatible = "cros-ec,boot-keys";
                down-arrow-rc = <KBD_RC(6, 11)>;
                esc-rc = <KBD_RC(1, 1)>;
                left-shift-rc = <KBD_RC(5, 7)>;
                refresh-rc = <KBD_RC(3, 2)>;
        };

        runtime-keys {
                compatible = "cros-ec,runtime-keys";
                vol-up-rc = <KBD_RC(0, 4)>;
                left-alt-rc = <KBD_RC(0, 10)>;
                right-alt-rc = <KBD_RC(6, 10)>;
                h-rc = <KBD_RC(1, 6)>;
                r-rc = <KBD_RC(7, 3)>;
        };
        ...
};
```

### Factory test

The factory test debugging command needs its own configuration, specifying
which GPIOs are connected to each specific pin in the keyboard connector, refer
to the device schematics and define a node like:

```
&kbd {
        ...
        factory-test {
                compatible = "cros-ec,keyboard-factory-test";

                pin1-gpios = <&gpioksoh 4 GPIO_ACTIVE_HIGH>;
                pin2-gpios = <&gpioksoh 0 GPIO_ACTIVE_HIGH>;
                ...
                pin24-gpios = <&gpioksi 1 GPIO_ACTIVE_HIGH>;
                /* 25 nc */
                pin26-gpios = <&gpioksoh 5 GPIO_ACTIVE_HIGH>;
                /* 27 nc */
                pin28-gpios = <&gpioksoh 6 GPIO_ACTIVE_HIGH>;
        };
        ...
};
```

Only the pins that are actually connected need to be referenced.

### Vivaldi configuration

The Vivaldi configuration can be tweaked in the device tree in two different
ways: if there's only configuration required, just extend the default
`kbd_config_0` node directly, for example:

```
&kbd_config_0 {
        vivaldi-codes = <
                VIVALDI_TK_BACK              /* T1 */
                ...
                VIVALDI_TK_VOL_UP            /* T10 */
        >;
        capabilities = <0>;                  /* No lock button */
};
```

If multiple configurations are used or if the key map has to be changed as
well, extend the `vivaldi_kbd` instead, for example:

```
&vivaldi_kbd {
        vivaldi-keys = <
                KBD_RC(0, 2)   /* T1 */
                ...
                KBD_RC(0, 11)  /* T14 */
        >;

        kbd_config_0: kbd-config-0 {
                vivaldi-codes = <
                        VIVALDI_TK_BACK               /* T1 */
                        VIVALDI_TK_REFRESH            /* T2 */
                        VIVALDI_TK_FULLSCREEN         /* T3 */
                        VIVALDI_TK_OVERVIEW           /* T4 */
                        VIVALDI_TK_SNAPSHOT           /* T5 */
                        VIVALDI_TK_BRIGHTNESS_DOWN    /* T6 */
                        VIVALDI_TK_BRIGHTNESS_UP      /* T7 */
                        VIVALDI_TK_KBD_BKLIGHT_TOGGLE /* T8 */
                        VIVALDI_TK_DICTATE            /* T9 */
                        VIVALDI_TK_ACCESSIBILITY      /* T10 */
                        VIVALDI_TK_MICMUTE            /* T11 */
                        VIVALDI_TK_VOL_MUTE           /* T12 */
                        VIVALDI_TK_VOL_DOWN           /* T13 */
                        VIVALDI_TK_VOL_UP             /* T14 */
                >;
                capabilities = < (VIVALDI_KEYBD_CAP_FUNCTION_KEYS |
                                  VIVALDI_KEYBD_CAP_NUMERIC_KEYPAD |
                                  VIVALDI_KEYBD_CAP_SCRNLOCK_KEY |
                                  VIVALDI_KEYBD_CAP_ASSISTANT_KEY) >;
        };

        kbd_config_1: kbd-config-1 {
                vivaldi-codes = <
                        VIVALDI_TK_BACK             /* T1 */
                        VIVALDI_TK_REFRESH          /* T2 */
                        VIVALDI_TK_FULLSCREEN       /* T3 */
                        VIVALDI_TK_OVERVIEW         /* T4 */
                        VIVALDI_TK_SNAPSHOT         /* T5 */
                        VIVALDI_TK_BRIGHTNESS_DOWN  /* T6 */
                        VIVALDI_TK_BRIGHTNESS_UP    /* T7 */
                        VIVALDI_TK_DICTATE          /* T8 */
                        VIVALDI_TK_ACCESSIBILITY    /* T9 */
                        VIVALDI_TK_PLAY_PAUSE       /* T10 */
                        VIVALDI_TK_MICMUTE          /* T11 */
                        VIVALDI_TK_VOL_MUTE         /* T12 */
                        VIVALDI_TK_VOL_DOWN         /* T13 */
                        VIVALDI_TK_VOL_UP           /* T14 */
                >;
                capabilities = < (VIVALDI_KEYBD_CAP_FUNCTION_KEYS |
                                  VIVALDI_KEYBD_CAP_NUMERIC_KEYPAD |
                                  VIVALDI_KEYBD_CAP_SCRNLOCK_KEY |
                                  VIVALDI_KEYBD_CAP_ASSISTANT_KEY) >;
        };
};
```

The effective configuration can then be enabled in the board code by
implementing a `board_vivaldi_keybd_idx` function and referring to the child
node index, for example:

```
#include <drivers/vivaldi_kbd.h>

static bool key_bl = FW_KB_BL_NOT_PRESENT;

int8_t board_vivaldi_keybd_idx(void)
{
        if (key_bl == FW_KB_BL_NOT_PRESENT) {
                return DT_NODE_CHILD_IDX(DT_NODELABEL(kbd_config_1));
        } else {
                return DT_NODE_CHILD_IDX(DT_NODELABEL(kbd_config_0));
        }
}
```

## Testing and Debugging

### Shell Command

Command | Description | Usage
:------ | :---------- | :----
`ksstate` | Print the keyboard scanning mask state | `ksstate`
`kbpress` | Inject a fake press or release event | `kbpress <row> <col> <1 or 0>`
`kbfactorytest` | Scans the keyboard matrix pins and reports any detected short circuit | `kbfactorytest`
`input dump` | Enables printing of any input event (needs `CONFIG_INPUT_SHELL=y` and `CONFIG_INPUT_EVENT_DUMP=y` | `input dump <on or off>`
`input kbd_matrix_state_dump` | Enables printing of the keyboard matrix state and reports a keymask when deactivated | `input kbd_matrix_state_dump <device or off>`

[keyboard-rex]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/program/rex/keyboard.dtsi
[keyboard-brox]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/program/brox/keyboard.dtsi
[keyboard-jubilant]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/program/brox/jubilant/keyboard.dtsi
