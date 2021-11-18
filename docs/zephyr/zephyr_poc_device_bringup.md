# Zephyr Proof-of-Concept-Device Bringup

It may be useful to build a Zephyr OS-based EC for a device which
already has a CrOS EC device build, for the purposes of demonstrating
the feasibility of Zephyr OS.

This document is a work-in-progress list of tricks & tools that may be
useful to you.

## Initial Bringup

Initially, you'll want to get a basic UART functioning with nothing
but a shell and some basic console commands.

An example CL to do this for Lazor can be found
[here](https://crrev.com/c/2749765).

## Bringing up GPIOs

After you have UART functioning, GPIOs can be an easy target to start
unblocking further features.

We have a very ugly program to auto-generate the GPIO DTS based on
gpio.inc for the existing board.  You can find it at
`util/gpios_to_zephyr_dts.c`, and instructions are in the file on how
to compile and use it.  You may have to hand-modify the output.

The resultant CL for Lazor can be found [here](https://crrev.com/c/2749768).

## Bring up Host Commands

Set `CONFIG_PLATFORM_EC_HOSTCMD=y` and enable the appropriate
host-command interface for your platform (e.g., eSPI).

An example CL for Lazor can be found [here](https://crrev.com/c/2749428).

As long as you get this compiling that should be enough to move to the
next step.  Further testing of the host command layer will require
power sequencing up and going.

## Enabling some simple GPIO-based buttons and switches

Next, you can:

* [Add the lid switch](https://crrev.com/c/2749768)
* [Add the power button](https://crrev.com/c/2749426)
* [Add AC presence detection](https://crrev.com/c/2749428)

## Power Sequencing

TODO(jrosenth): add steps on enabling power sequencing and expand this
document.
