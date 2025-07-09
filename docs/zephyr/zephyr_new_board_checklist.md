# Creating a New Zephyr EC Project

[TOC]

## Overview

This document describes the high-level steps needed to create a new Zephyr EC
project for a Chromebook design.

## Intended Audience

This document is for use by software engineers working in the EC codebase. This
document and the linked documents provide a guide for creating new Zephyr EC
projects and for modifying/maintaining existing Zephyr EC projects.

## How to use this document

The following sections detail a single feature set that needs modification for
your board design. Each feature set can be implemented with a reasonably sized
change list, and can be worked on independently.

Each feature includes the following sub-tasks:

- **Kconfig Options** - This section details the `Kconfig` options relevant to
  the feature. `Kconfig` options are enabled in one of the [project
  configuration files].
- **Devicetree Nodes** - This section details the devicetree nodes and
  properties required by the feature. The [Zephyr Introduction to Devicetree]
  provides a conceptual overview of devicetree and how Zephyr uses it.
- **Board Specific Code** - When present, this section details any C code that
  your project must implement.
- **Threads** - This section details the threads created by the feature and
  provides an overview of each thread.
- **Testing and Debugging** - This section details strategies for testing the EC
  feature set and for debugging issues. This section also documents EC console
  commands related to the feature set.
- **Example** - This section walks through a complete example for configuring an
  EC feature based on existing board implementation.

## Adding a new project to zmake

Refer the [project configuration](project_config.md) documentation to add a new
board project to zmake.

## Configure EC Features

The checklist below provides an overview of EC features that you must configure
for correct operation of a Chromebook. The “Needed for Power On” column
indicates the critical features needed during board bringup. Use the
documentation link for details about the code changes required to implement each
feature.

EC Feature                                                                  | Needed for Power On
:-------------------------------------------------------------------------- | :-----------------:
[Configure EC Chipset (TODO)](./zephyr_template.md)                         | yes
[Configure AP to EC Communication](./zephyr_ap_ec_comm.md)                  | yes
[Configure AP Power Thresholds](./zephyr_ap_power.md)                       | yes
[Configure AP Power Sequencing (TODO)](./zephyr_template.md)                | yes
[Configure USB-A](./zephyr_usba.md)                                         | no
[Configure USB-C](#usb-c-ports)                                             | yes
[Configure Charger](./zephyr_charger.md)                                    | yes
[Configure I2C Buses](./zephyr_i2c.md)                                      | yes
[Configure GPIO](./zephyr_gpio.md)                                          | yes
[Configure Batteries](./zephyr_battery.md)                                  | no
[Configure CrOS Board Information (CBI)](./zephyr_cbi.md)                   | no
[Configure Keyboard](./zephyr_keyboard.md)                                  | no
[Configure LEDs](./zephyr_leds.md)                                          | no
[Configure Motion Sensors](./zephyr_motionsense.md)                         | no
[Configure BC1.2 Charger Detector](./zephyr_bc12.md)                        | no
[Configure ADC](./zephyr_adc.md)                                            | no
[Configure Temperature Sensors](./zephyr_temperature_sensor.md)             | no

### USB-C Ports
The Zephyr EC application supports two different technology stacks to support
USB Type-C ports and USB PD (Power Delivery).  Click on the links for
instructions specific to each technology stack.

1. [TCPMv2](./zephyr_usbc.md): Projects created before 2024 implement the TCPMv2
(Type-C Port Manager version 2) technology stack.
1. [PDC](./pdc.md): Projects created 2024 and later support USB Type-C ports
using Power Delivery Controller (PDC) chips.


[Zephyr Introduction to Devicetree]: https://docs.zephyrproject.org/latest/build/dts/intro.html
