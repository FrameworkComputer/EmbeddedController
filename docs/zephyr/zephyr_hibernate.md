# Zephyr Hibernate Configuration

[TOC]

## Overview

Hibernate support puts the EC into the lowest operating power state. The EC
hibernate is distinct from the AP power states.

The EC is blocked from entering hibernation unless the following conditions are
true.

* AP power state is off (G3 or currently transitioning to G3 from S5).
* AC power is disconnected.
* Hibernate timer has expired.

EC chip types support different mechanisms for entering and exiting hibernate,
but there are some common characteristics:

* SRAM contents are not retained through hibernate
* Wake-up from hibernate is equivalent to a cold reset. The EC re-executes RO
code and jumps to RW code.

## Kconfig Options

The Kconfig option [`CONFIG_PLATFORM_EC_HIBERNATE`] enables hibernation support
in the EC application. The Kconfig option [`CONFIG_PLATFORM_EC_HIBERNATE_TYPE`]
selects the hibernate driver used by the platform.

There are 5 types of hibernate supported.  Click through to the [Kconfig.system]
file for details on each type of hibernation.

1. [`CONFIG_PLATFORM_EC_HIBERNATE_PSL`]
1. [`CONFIG_PLATFORM_EC_HIBERNATE_VCI`]
1. [`CONFIG_PLATFORM_EC_HIBERNATE_ELPM`]
1. [`CONFIG_PLATFORM_EC_HIBERNATE_WAKE_PINS`]
1. [`CONFIG_PLATFORM_EC_HIBERNATE_Z5`]

The hibernate type is automatically configured based on the which devicetree
nodes are included in your project.

## Devicetree Nodes

### `CONFIG_PLATFORM_EC_HIBERNATE_PSL`

Supported by the Nuvoton NPCX families only. The `enabled-gpios` property in
the [`nuvoton,npcx-power-psl`] devictree node configures the GPIO corresponding
to the PSL_OUT signal.

The PSL_OUT signal is fixed based on the NPCX family. For reference, the
assignment of `enable-gpios` is noted below.

* [NPCX7 PSL_OUT]
* [NPCX9 PSL_OUT]

Use the `pinctrl-0` property to specify the list of PSL input (wake sources)
for the platform.

The PSL input wake sources varies based on the NPCX chip family.

* [NPCX7 PSL Inputs]
* [NPCX9 PSL Inputs]

For each PSL input, set the trigger mode and polarity of the signal. The set
the `pinctrl-0` property to the list of PSL inputs.

The final steps are to make sure the [`nuvoton,npcx-power-psl`] node is enabled
and the and the `pinctrl-names` property is set to `"sleep"`.

Example from the [Rex project]:

```
/* Power switch logic input pads */
&psl_in1_gpd2 {
	/* LID_OPEN */
	psl-in-mode = "edge";
	psl-in-pol = "high-rising";
};

&psl_in2_gp00 {
	/* ACOK_OD */
	psl-in-mode = "edge";
	psl-in-pol = "high-rising";
};

&psl_in4_gp02 {
	/* MECH_PWR_BTN_ODL */
	psl-in-mode = "edge";
	psl-in-pol = "low-falling";
};

/* Power domain device controlled by PSL (Power Switch Logic) IO pads */
&power_ctrl_psl {
	status = "okay";
	pinctrl-names = "sleep";
	pinctrl-0 = <&psl_in1_gpd2 &psl_in2_gp00 &psl_in4_gp02>;
};

```

#### Hibernate Wake Source Detection

The application can determine the source of the wake-up from hibernate using the
`cros_system_get_hibernate_wake_source` API. Currently, this feature is only
implemented for NPCX based platforms. Other platforms will return `-ENOSYS`.

For NPCX based platforms using PSL, the driver maps the PSL input that caused
the wake to a specific wake source (e.g., AC, Lid, Power Button). To enable
this mapping, you must label the corresponding PSL input nodes in the
devicetree with `wake_source_acok`, `wake_source_lid_open`, and
`wake_source_pwr_btn`.

Example from the [Bluey project]:

```
/* Power switch logic input pads */
wake_source_lid_open: &psl_in1_gpd2 {
	/* EC_LID_OPEN */
	psl-in-mode = "edge";
	psl-in-pol = "high-rising";
};

wake_source_pwr_btn: &psl_in2_gp00 {
	/* EC_PWR_BTN_ODL */
	psl-in-mode = "edge";
	psl-in-pol = "low-falling";
};

wake_source_acok: &psl_in3_gp01 {
	/* EC_ACOK_OD */
	psl-in-mode = "edge";
	psl-in-pol = "high-rising";
};
```

### `CONFIG_PLATFORM_EC_HIBERNATE_VCI`

Supported by the Microchip EC family only.  An example configuration of
the [`cros-ec,hibernate-vci-pin`] driver is shown below.  This comes from the
[ptlrvp_mchp] project.


```
/ {
	vci-pins{
		#address-cells = <1>;
		#size-cells = <0>;
		/**
		 * Platform supports two wake-up sources:
		 * VCI_IN0: Power Button
		 * VCI_OVRD_IN: AC Present (using default register values)
		 *
		 * VCI_IN1: BATT_ID is enabled to allow system to reset when
		 *          only battery is power source
		 *
		 * Note: Lid is not a wake-up source.
		*/
		vci_power_btn: vci_input_0@0 {
			compatible = "cros-ec,hibernate-vci-pin";
			reg = <0>;
			vci-polarity = "Active_Low";
			vci-latch-enable;
			wakeup;
			status = "okay";
		};
		vci_batt_id: vci_input_1@1 {
			compatible = "cros-ec,hibernate-vci-pin";
			reg = <1>;
			vci-polarity = "Active_Low";
			vci-latch-enable;
			preserve;
			status = "okay";
		};
	};
};
```

For details about configuring the VCI pin properties, refer to the
[`cros-ec,hibernate-vci-pin`] schema file.

### `CONFIG_PLATFORM_EC_HIBERNATE_ELPM`

Supported exclusively on ITE SoC families. Here is the example overlay
setting using the IT82000.BW SoC:

```
&power_ctrl_elpm {
	status = "okay";
	pinctrl-0 = <&xlpin0_gpq0_default>;
	pinctrl-names = "default";

	/* XLPIN[0](GPIO_G1): enabled, low-falling */
	xlpin0: xlpin@0 { reg = <0>; polarity = "low-falling"; };
};
```

### `CONFIG_PLATFORM_EC_HIBERNATE_WAKE_PINS`

Supported by the ITE, Nuvoton, and Microchip ECs. The
[`cros-ec,hibernate-wake-pins`] node defines the interrupt signals that
will wake the EC from hibernate.

```
/ {
	hibernate-wake-pins {
		compatible = "cros-ec,hibernate-wake-pins";
		wakeup-irqs = <&int_ac_present
			       &int_power_button
			       &int_lid_open>;
	};
};
```

Each entry in the `wakeup-irqs` array references a platform interrupt signal
configured by the `"cros-ec,gpio-interrupts"` devictree node. Refer to the
[GPIO configuration](./zephyr_gpio.md) documentation for more information.

### `CONFIG_PLATFORM_EC_HIBERNATE_Z5`

The Hibernate Z5 implementation is supported by all EC chip types. The
[`cros-ec,hibernate-z5`] mode defines the GPIO connected to the EC used to
force the platform into the Z5 power state.

Discrete logic on the platform is responsible for transitioning the platform
back to Z1 and powering up the EC when a wake source is activated.

Example configuration from the [brox project]:

```
/{
	hibernate_z5: hibernate-z5 {
		compatible = "cros-ec,hibernate-z5";
		en-slp-z-gpios = <&gpioa 0 GPIO_ACTIVE_HIGH>;
	};
};
```
## Threads

EC hibernate support does not enable a dedicated thread. The hibernate support
monitors AP power state changes, and starts a timer when the AP power state
transitions to off.

## Testing and Debugging

### Shell Commands

Command | Description | Usage
:------ | :---------- | :----
`hibernate` | Hibernate the EC | `hibernate [seconds] [microseconds]`
`hibdelay` | Set the delay before going into hibernation | `hibdelay <seconds>`

Use the `hibernate` command to request the EC hibernate before the project
hibernation timeout expires. The AP power state must be off and AC must be
disconnected before the EC enters hibernate.

To verify the EC entered hibernate, shutdown the AP, disconnect AC, and run
the `hibernate` shell command. The EC should not respond to UART input, and
the EC should only wake up from hibernate for the configured wake sources. The
typical hibernate wake sources are:

* Power button press
* AC connection
* Lid open

Suspend wake sources, such as keyboard press and lid angle change, should not
wake the EC from hibernation.

Use the `hibdelay` command to override the project's hibernation timeout.


[`CONFIG_PLATFORM_EC_HIBERNATE`]:https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.system?q=%22config%20PLATFORM_EC_HIBERNATE%22
[`CONFIG_PLATFORM_EC_HIBERNATE_TYPE`]:https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.system?q=%22choice%20PLATFORM_EC_HIBERNATE_TYPE%22
[Kconfig.system]:https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.system
[`CONFIG_PLATFORM_EC_HIBERNATE_PSL`]:https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.system?q=%22config%20PLATFORM_EC_HIBERNATE_PSL%22
[`CONFIG_PLATFORM_EC_HIBERNATE_VCI`]:https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.system?q=%22config%20PLATFORM_EC_HIBERNATE_VCI%22
[`CONFIG_PLATFORM_EC_HIBERNATE_ELPM`]:https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.system?q=%22config%20PLATFORM_EC_HIBERNATE_ELPM%22
[`CONFIG_PLATFORM_EC_HIBERNATE_WAKE_PINS`]:https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.system?q=%22config%20PLATFORM_EC_HIBERNATE_WAKE_PINS%22
[`CONFIG_PLATFORM_EC_HIBERNATE_Z5`]:https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.system?q=%22config%20PLATFORM_EC_HIBERNATE_Z5%22
[`nuvoton,npcx-power-psl`]:https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/third_party/zephyrproject/zephyr/dts/arm/nuvoton/npcx/npcx.dtsi?q=%22nuvoton,npcx-power-psl%22
[NPCX7 PSL_OUT]:https://source.corp.google.com/h/chrome-internal/chromeos/superproject/+/main:src/platform/ec/zephyr/boards/google/npcx7/npcx7.dts?q=enable-gpios
[NPCX9 PSL_OUT]:https://source.corp.google.com/h/chrome-internal/chromeos/superproject/+/main:src/platform/ec/zephyr/boards/google/npcx9/npcx9.dts?q=enable-gpios
[NPCX7 PSL inputs]:https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/third_party/zephyrproject/zephyr/dts/arm/nuvoton/npcx/npcx7/npcx7-pinctrl.dtsi?q=%22PSL%20peripheral%20interfaces%22
[NPCX9 PSL inputs]:https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/third_party/zephyrproject/zephyr/dts/arm/nuvoton/npcx/npcx9/npcx9-pinctrl.dtsi?q=%22PSL%20peripheral%20interfaces%22
[Rex project]:https://source.corp.google.com/h/chrome-internal/chromeos/superproject/+/main:src/platform/ec/zephyr/program/rex/rex.dtsi?q=%22Power%20switch%20logic%22
[`cros-ec,hibernate-vci-pin`]:https://source.corp.google.com/h/chrome-internal/chromeos/superproject/+/main:src/platform/ec/zephyr/dts/bindings/gpio/cros-ec,hibernate-vci-pin.yaml
[ptlrvp_mchp]:https://source.corp.google.com/h/chrome-internal/chromeos/superproject/+/main:src/platform/ec/zephyr/program/intelrvp/ptlrvp/ptlrvp_mchp/vci.dtsi;l=7?q=vci-pins
[`cros-ec,hibernate-wake-pins`]:https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/dts/bindings/gpio/cros-ec,hibernate-wake-pins.yaml
[`cros-ec,hibernate-z5`]:https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/dts/bindings/hibernate/cros-ec,hibernate-z5.yaml
[brox project]:https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/program/brox/hibernate.dtsi
[Bluey project]:https://source.corp.google.com/h/chrome-internal/chromeos/superproject/+/main:src/platform/ec/zephyr/program/bluey/psl.dtsi
