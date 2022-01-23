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
  properties required by the feature.
- **Board Specific Code** - When present, this section details any C code that
  your project must implement.
- **Threads** - This section details the threads created by the feature and
  provides an overview of each thread.
- **Testing and Debugging** - This section details strategies for testing the EC
  feature set and for debugging issues. This section also documents EC console
  commands related to the feature set.
- **Example** - This section walks through a complete example for configuring an
  EC feature based on existing board implementation.

## Adding a new board to zmake

Refer the [zmake](TODO) documentation to add a new board project to zmake.

## Configure EC Features

The checklist below provides an overview of EC features that you must configure
for correct operation of a Chromebook. The “Needed for Power On” column
indicates the critical features needed during board bringup. Use the
documentation link for details about the code changes required to implement each
feature.

EC Feature                                                                  | Needed for Power On
:-------------------------------------------------------------------------- | :-----------------:
[Configure EC Chipset (TODO)](./zephyr_template.md)                         | yes
[Configure AP to EC Communication (TOD0)](./zephyr_template.md)             | yes
[Configure AP Power Sequencing (TODO)](./zephyr_template.md)                | yes
[Configure USB-C (TODO)](./zephyr_template.md)                              | yes
[Configure Charger (TODO)](./zephyr_template.md)                            | yes
[Configure I2C Buses](./zephyr_i2c.md)                                      | yes
[Configure GPIO](./zephyr_gpio.md)                                          | yes
[Configure Batteries (TODO)](./zephyr_template.md)                          | no
[Configure CrOS Board Information (CBI) (TODO)](./zephyr_template.md)       | no
[Configure CrOS CBI FW CONFIG](./zephyr_fw_config.md)                       | no
[Configure Keyboard (TODO)](./zephyr_template.md)                           | no
[Configure LEDs (TODO)](./zephyr_template.md)                               | no
[Configure Motion Sensors (TODO)](./zephyr_template.md)                     | no
[Configure BC1.2 Charger Detector (TODO)](./zephyr_template.md)             | no
[Configure Battery (TODO)](./zephyr_template.md)                            | no
[Configure ADC](./zephyr_adc.md)                                            | no
[Configure Temperature Sensors](./zephyr_temperature_sensor.md)             | no
