# Zephyr EC Feature Configuration Template

[TOC]

## Overview

*Description of the Zephyr EC feature and the capabilities provided*

## Kconfig Options

*Link to the file providing all the Kconfig options related to the feature.  If
the Kconfig options are not currently in a standalone file, consider moving the
related Kconfigs into their own file.*

*Example CL moving I2C related configs into a new file: https://crrev.com/c/3575081*

*Note - Avoid documenting `CONFIG_` options in the markdown as the relevant
`Kconfig*` contains the authoritative definition. If there is one main Kconfig
that must be enabled for the feature, mention it in this section. See the [I2C
documentation](zephyr_i2c.md#kconfig-options) for an example.*

*If the `Kconfig` file does not provide sufficient help descriptions, please fix
them.*

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

*It's especially helpful to document EC console commands and `ectool` commands
from the AP in this section.  Please provide example output.*

## Example

*Provide code snippets from a working board to walk the user through
all code that must be created to enable this feature.*

<!--
The following demonstrates linking to a code search result for a Kconfig option.
Reference this link in your text by matching the text in brackets exactly.
-->
[I2C Passthru Restricted]:https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig?q=%22config%20PLATFORM_EC_I2C_PASSTHRU_RESTRICTED%22&ss=chromiumos
