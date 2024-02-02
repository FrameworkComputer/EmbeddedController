# Renode

This directory holds the configuration files for Renode.

## Hardware WP

You can type the following into the renode console to enable/disable HW GPIO:

Action            | Renode command for `bloonchipper`
----------------- | ----------------------------------
**Enable HW-WP**  | `sysbus.gpioPortB.GPIO_WP Release`
**Disable HW-WP** | `sysbus.gpioPortB.GPIO_WP Press`

Note, you can just type `sysbus`, `sysbus.gpioPortB`, or
`sysbus.gpioPortB.GPIO_WP` to learn more about these modules and the available
functions.
