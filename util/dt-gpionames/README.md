# dt-gpionames

## Overview

This program reads a compiled device tree file (DTB), and creates
a device tree source (DTS) fragment that initialises the `gpio-line-names`
for each GPIO controller.

The `named-gpios` compatible node is used as the source of the names. Each child node
of the `named-gpios` node is named from a EC pin name, has a `gpios` property
referencing the GPIO pin.

The program matches the GPIO controller and pin for each of the
`named-gpios` entries, and creates overlay DTS fragments for each of
the GPIO controllers setting the `gpio-line-names`.

The purpose of this program is to help upstream the EC GPIO support,
specifically to enable a Zephyr based console command equivalent
to `gpioget` and `gpioset`.

Currently these commands rely on the existence of the
`named-gpios` child nodes to specify the engineer-friendly name of
the GPIO pins. However, core Zephyr does not use `named-gpios`,
and any command upstreamed to Zephyr should only rely on
the core Zephyr API and interfaces (and the `gpio-line-names`
property is a standard attribute of the Zephyr GPIO definition).

Using this program, a DTS fragment can be generated from an existing
DTS with a `named-gpios` node that will allow an upstreamed
`gpioget`/`gpioset` command to operate.

## Building

```
cd platform/ec/util/dt-gpionames
go build

```

This builds the `dt-gpionames` binary in this directory.

## Executing

The program input expects a compiled, flattened device tree binary (DTB)
file.

The source file for the compiler can be obtained from a device tree
source file generated as part of a project build.

So the complete process is (using the nivviks project target as an example):

```
[within the SDK]
cd ../src/platform/ec/zephyr
zmake configure -b nivviks
cd ../util/dt-gpionames
dtc --out /tmp/dt.dtb ../../build/zephyr/nivviks/build-ro/zephyr/zephyr.dts
./dt-gpionames --output gpionames.dts --input /tmp/dt.dtb
```

This `gpionames.dts` can then be copied and added to the project's
device tree source files.

A shell script is available that runs these commands as a
single step:


```
gpionames.sh <project>
```

Where `project` is the project target e.g `nivviks`, `lazor` etc.
The script will place the output file in `/tmp/gpionames-`<project>`.dts`.

## Examples

If the `named-gpios` node contained:
```
	named-gpios {
		compatible = "named-gpios";

		gpio_slp_s4_l: slp_s4_l {
			#gpio-cells = < 0x0 >;
			gpios = < &gpio7 0x0 0x100 >;
		};
		gpio_ec_soc_pch_pwrok_od: ec_soc_pch_pwrok_od {
			#gpio-cells = < 0x0 >;
			gpios = < &gpio7 0x2 0xa06 >;
		};
		gpio_ec_gsc_packet_mode: ec_gsc_packet_mode {
			#gpio-cells = < 0x0 >;
			gpios = < &gpio7 0x5 0x100 >;
			enum-name = "GPIO_PACKET_MODE_EN";
		};
		gpio_ec_soc_rtcrst: ec_soc_rtcrst {
			#gpio-cells = < 0x0 >;
			gpios = < &gpio7 0x6 0x200 >;
		};
...
	};
```

The GPIO controllers DTS fragment would be:

```
/ {
        soc {
                gpio@4008f000 {
                        gpio-line-names =
                                "slp_s4_l",
                                "",
                                "ec_soc_pch_pwrok_od",
                                "",
                                "",
                                "ec_gsc_packet_mode",
                                "ec_soc_rtcrst";
                };
	};
};
```

## Testing

Since this is considered a one-off utility, there are no unit tests yet.
