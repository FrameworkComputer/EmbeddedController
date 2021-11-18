# Zephyr EC Feature Configuration Template

[TOC]

## Overview

*Description of the Zephyr EC feature and the capabilities provided*

## Kconfig Options

*List the Kconfig options that enable the feature and list any sub-configuration
options that control the behavior of the feature.*

Kconfig Option                         | Default | Documentation
:------------------------------------- | :-----: | :------------
`CONFIG_PLATFORM_EC_<option>`          | y/n     | [zephyr/Kconfig](../zephyr/Kconfig)

Kconfig sub-option                     | Default | Documentation
:------------------------------------- | :-----: | :------------
`CONFIG_PLATFORM_EC_<option>`          | y/n     | [zephyr/Kconfig](../zephyr/Kconfig)


*Note - Avoid documenting `CONFIG_` options in the markdown as the relevant
`Kconfig*` contains the authoritative definition. Link directly to the Kconfig
option in source like this: [I2C Passthru Restricted].*

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

<!--
The following demonstrates linking to a code search result for a Kconfig option.
Reference this link in your text by matching the text in brackets exactly.
-->
[I2C Passthru Restricted]:https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig?q=%22config%20PLATFORM_EC_I2C_PASSTHRU_RESTRICTED%22&ss=chromiumos
