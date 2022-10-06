# MKBP - Matrix Keyboard Protocol

[TOC]

## Overview

[MKBP] was originally used to send keyboard events to AP OS.
Later, more functionalities were added and more types of events can be sent
using this protocol. It can transfer information about keystrokes, sensors,
switches, fingerprints and more.

Kconfig sub-options                          | Default     | Documentation
:------------------------------------------- | :---------: | :------------
`CONFIG_PLATFORM_EC_MKBP_INPUT_DEVICES`      | n           | [MKBP input devices]
`CONFIG_PLATFORM_EC_MKBP_EVENT`              | n           | [MKBP event]

## Delivery method

The following options select the delivery method of MKBP messages.

Kconfig sub-options                              | Default     | Documentation
:----------------------------------------------- | :---------: | :------------
`CONFIG_PLATFORM_EC_MKBP_USE_GPIO`               | y           | [MKBP gpio]
`CONFIG_PLATFORM_EC_MKBP_USE_HOST_EVENT`         | n           | [MKBP host event]
`CONFIG_PLATFORM_EC_MKBP_USE_GPIO_AND_HOST_EVENT`| n           | [MKBP gpio and host event]
`CONFIG_PLATFORM_EC_MKBP_USE_CUSTOM`             | n           | [MKBP custom]

## Wake-up masks

The EC can wake up the AP from sleep modes based on multiple event types.
A wake-up mask specifies which specific event types are able to wake the AP.

For x86 based Chromebooks, the wake-up mask is controlled by the AP firmware
directly using host commands `EC_CMD_HOST_EVENT_SET_SMI_MASK`,
`EC_CMD_HOST_EVENT_SET_SCI_MASK` and `EC_CMD_HOST_EVENT_SET_WAKE_MASK`.

For ARM based Chromebooks, the wake-up mask is defined as device tree nodes,
one for MKBP events and one for generic host events.
They have respective Kconfig options, which must be enabled to take the masks
into account.

### Kconfigs

The Kconfigs responsible for enabling the wake-up masks are automatically
selected if the device tree nodes are specified.
See [Device Tree nodes](#device-tree-nodes) paragraph for details.

### Device Tree nodes

Both masks have to be compatible with binding file: [MKBP event mask yaml]

Possible enums to use in these nodes are specified in file: [MKBP event mask enums]

```
/ {
	ec-mkbp-host-event-wakeup-mask {
		compatible = "ec-wake-mask-event";
		wakeup-mask = <(HOST_EVENT_LID_OPEN |
				HOST_EVENT_POWER_BUTTON |
				HOST_EVENT_AC_CONNECTED |
				HOST_EVENT_AC_DISCONNECTED |
				HOST_EVENT_HANG_DETECT |
				HOST_EVENT_RTC |
				HOST_EVENT_MODE_CHANGE |
				HOST_EVENT_DEVICE)>;
	};

	ec-mkbp-event-wakeup-mask {
		compatible = "ec-wake-mask-event";
		wakeup-mask = <(MKBP_EVENT_KEY_MATRIX |
				MKBP_EVENT_HOST_EVENT |
				MKBP_EVENT_SENSOR_FIFO)>;
	};
}
```

## Examples

[Lazor wake-up masks](https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/projects/trogdor/lazor/gpio.dts?q=ec-mkbp-host-event-wakeup-mask)

For detailed descriptions of the MKBP and host event types, please see
[ec_commands.h](/include/ec_commands.h) header file.

<!--
Links to the documentation
-->

[MKBP]:../ec_terms.md#mkbp

[MKBP input devices]:https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig?q=%22config%20PLATFORM_EC_MKBP_INPUT_DEVICES%22
[MKBP event]:https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig?q=%22config%20PLATFORM_EC_MKBP_EVENT%22

[MKBP gpio]:https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.mkbp_event?q=%22config%20PLATFORM_EC_MKBP_USE_GPIO%22
[MKBP host event]:https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.mkbp_event?q=%22config%20PLATFORM_EC_MKBP_USE_HOST_EVENT%22
[MKBP gpio and host event]:https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.mkbp_event?q=%22config%20PLATFORM_EC_MKBP_USE_GPIO_AND_HOST_EVENT%22
[MKBP custom]:https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.mkbp_event?q=%22config%20PLATFORM_EC_MKBP_USE_CUSTOM%22

[MKBP event mask yaml]:https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/dts/bindings/cros_mkbp_event/ec-mkbp-event.yaml
[MKBP event mask enums]:https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/include/dt-bindings/wake_mask_event_defines.h
