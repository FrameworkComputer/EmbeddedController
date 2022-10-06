# Application Processor to EC communication

[TOC]

## Overview

Host commands allow communication between AP and EC.

This communication is used to inform the ChromeOS running on AP about events
like opening or closing lid, connecting or disconnecting charger,
thermal status, keyboard events.

## Kconfig options

Kconfig Option                           | Default     | Documentation
:--------------------------------------- | :---------: | :------------
`CONFIG_PLATFORM_EC_HOSTCMD`             | y if AP     | [EC host commands]

### Transport layer

The `CONFIG_PLATFORM_EC_HOST_INTERFACE_TYPE` choice selects the interface type
used to transport the AP/EC messages.

Kconfig to select interface              | Default     | Documentation
:--------------------------------------- | :---------: | :------------
`CONFIG_PLATFORM_EC_HOST_INTERFACE_ESPI` | y if AP_X86 | [eSPI interface]
`CONFIG_PLATFORM_EC_HOST_INTERFACE_HECI` | n           | [HECI interface]
`CONFIG_PLATFORM_EC_HOST_INTERFACE_LPC`  | n           | [LPC interface]
`CONFIG_PLATFORM_EC_HOST_INTERFACE_SHI`  | y if AP_ARM | [SHI interface]

#### SHI

[SHI] is acronym for SPI Host Interface. It's default interface for computers
with ARM AP.
It doesn't have any special Kconfigs.

#### eSPI - Virtual wires

[eSPI] is acronym for Enhanced Serial Peripheral Interface.
It allows to define signals as virtual ones instead of using normal GPIOs.

See the [Kconfig.espi] file for all sub-options related to the eSPI interface.

#### HECI

HECI is acronym for Host Embedded Controller Interface.
It doesn't have any special Kconfigs.

#### LPC

[LPC] is acronym for Low Pin Count bus.
It doesn't have any special Kconfigs.

### Generic configuration

The following options are generic and defines features available through
host interface.

Kconfig sub-options                          | Default     | Documentation
:------------------------------------------- | :---------: | :------------
`CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE`         | y           | [Host command console]
`CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE_BUF_SIZE`| 4096        | [Host command console buffer size]
`CONFIG_PLATFORM_EC_HOST_COMMAND_STATUS`     | y if PLATFORM_EC_HOST_INTERFACE_SHI | [Host command - status]

### MKBP - Matrix Keyboard Protocol

See the [MKBP documentation] for information about configuration of MKBP
protocol.

### Debug

The `CONFIG_PLATFORM_EC_HOSTCMD_DEBUG_MODE` choice selects the verbosity
level of messages on the EC console.

Kconfig debug verbosity | Default | Documentation
:---------------------- | :-----: | :------------
`CONFIG_HCDEBUG_OFF`    | n       | [Debug off]
`CONFIG_HCDEBUG_NORMAL` | y       | [Debug normal]
`CONFIG_HCDEBUG_EVERY`  | n       | [Debug every]
`CONFIG_HCDEBUG_PARAMS` | n       | [Debug params]

## Device Tree nodes

### eSPI

Zephyr has built-in support for eSPI and EC takes advantage of this.
Boards supported by Zephyr should have definitions of eSPI interfaces in their
device trees.
To select the eSPI interface to be used for sending the host commands it must
be set as chosen node with name `cros-ec,espi`.
The referenced node has to be enabled, which means that its `status` must
be set as `okay`.

```
/ {
	chosen {
		cros-ec,espi = &espi0;
	}
}

&espi0 {
	status = "okay";
};
```

### SHI

The SoC specific Kconfig for SHI driver is automatically enabled based on
selected host interface and SoC family. Each SoC driver has specific
compatibility string which is used to get node with configuration from the
device tree.
For example, the nuvoton npcx chip uses compatibility string
`nuvoton,npcx-cros-shi`.

The node's required properties are defined in yaml files: [SHI bindings]

```
/ {
	shi: shi@4000f000 {
		compatible = "nuvoton,npcx-cros-shi";
		reg = <0x4000f000 0x120>;
		interrupts = <18 1>;
		clocks = <&pcc NPCX_CLOCK_BUS_APB3 NPCX_PWDWN_CTL5 1>;
		pinctrl-0 = <&altc_shi_sl>;
		shi-cs-wui =<&wui_io53>;
		label = "SHI";
	};
}
```

## Board Specific Code

No board specific code is required.

## Threads

The host_command_task() thread processes all requests from the AP, regardless
of the host interface type enabled.

Enabling the `CONFIG_PLATFORM_EC_HOSTCMD` automatically selects
`CONFIG_HAS_TASK_HOSTCMD` which is responsible for creating the thread.

## Testing and Debugging

Testing and debugging the host commands on DUT are described in
[CrOS EC documentation]. It is recommended to run the stress test to check
if communication is working properly with a huge amount of data sent.
Running the stress test is described in the previously mentioned documentation,
in the chapter [stress test].

### From EC console

If debug level is higher than `CONFIG_HCDEBUG_OFF`, then HC messages can
be seen on EC console.

If there's no output lines starting from HC, that may be due to hostcmd channel
being masked.
To check what channels are masked, execute `chan` command in EC console.
```
uart:~$ chan
 # Mask     E Channel
...
 7 00000080 * hostcmd
```
The asterisk next to channel name means that it is enabled. Otherwise, execute
command `chan 128` to enable hostcmd channel while disabling others.

If the hostcmd channel is disabled by default, it may be enabled by removing
its name from `ec-console` node.

```
ec-console {
	compatible = "ec-console";

	disabled = "hostcmd";
};
```

Removing the `hostcmd` and leaving the empty quotes will result in hostcmd
messages visible on EC console since boot-up.

## Examples

[NPCX eSPI selection](https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/include/cros/nuvoton/npcx.dtsi?q=cros-ec,espi)

[NPCX SHI definition](https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/include/cros/nuvoton/npcx.dtsi?q=shi)

<!--
Links to the documentation
-->
[SHI]:../ec_terms.md#shi
[eSPI]:../ec_terms.md#espi
[LPC]:../ec_terms.md#lpc
[EC host commands]:https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig?q=%22menuconfig%20PLATFORM_EC_HOSTCMD%22
[eSPI interface]:https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.host_interface?q=%22config%20PLATFORM_EC_HOST_INTERFACE_ESPI%22
[HECI interface]:https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.host_interface?q=%22config%20PLATFORM_EC_HOST_INTERFACE_HECI%22
[LPC interface]:https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.host_interface?q=%22config%20PLATFORM_EC_HOST_INTERFACE_LPC%22
[SHI interface]:https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.host_interface?q=%22config%20PLATFORM_EC_HOST_INTERFACE_SHI%22

[Kconfig.espi]:https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.espi

[Host command console]:https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.console?q=%22menuconfig%20PLATFORM_EC_HOSTCMD_CONSOLE%22
[Host command console buffer size]:https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.console?q=%22config%20PLATFORM_EC_HOSTCMD_CONSOLE_BUF_SIZE%22
[Host command - status]:https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig?q=%22config%20PLATFORM_EC_HOST_COMMAND_STATUS%22

[MKBP documentation]:zephyr_mkbp.md

[Debug off]:https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig?q=%22config%20HCDEBUG_OFF%22
[Debug normal]:https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig?q=%22config%20HCDEBUG_NORMAL%22
[Debug every]:https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig?q=%22config%20HCDEBUG_EVERY%22
[Debug params]:https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig?q=%22config%20HCDEBUG_PARAMS%22

[SHI bindings]:https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/dts/bindings/cros_shi/

[CrOS EC documentation]:../ap-ec-comm.md#ectool
[stress test]:../ap-ec-comm.md#stress-test
