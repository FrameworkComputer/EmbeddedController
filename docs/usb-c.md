# EC Implementation of USB-C Power Delivery and Alternate Modes

USB-C PD requires a complex state machine as USB-C PD can operate in many
different modes. This includes but isn't limited to:

*   Negotiated power contracts. Either side of the cable can source or sink
    power up to 100W (if supported by device).
*   Reversed cable mode. This requires a mux to switch the signals before
    getting to the SoC (or AP).
*   Debug accessory mode, e.g. [Case Closed Debugging (CCD)]
*   Multiple uses for the 4 differential pair signals including
    *   USB SuperSpeed mode (up to 4 lanes for USB data)
    *   DisplayPort Alternate Mode (up to 4 lanes for DisplayPort data)
    *   Dock Mode (2 lanes for USB data, and 2 lanes for DisplayPort)
    *   Audio Accessory mode. (1 lane is used for L and R analog audio signal)

For a more complete list of USB-C Power Delivery features, see the
[USB-C PD spec][USB PD Spec Id].

This document covers various touch points to consider for USB-C PD and Alternate
Modes in the EC codebase.

[TOC]

## Glossary

*   PD {#pd}
    *   Power Delivery. Protocol over USB-C connector that allows up to 100W of
        power. Not supported on USB-A or USB-B connectors. A good overview of
        USB PD is found in the [Introduction to USB Power Delivery] application
        note.
*   TCPC {#tcpc}
    *   Type-C Port Controller. Typically a separate IC connected through I2C,
        sometimes embedded within the EC as a hardware sub module. The TCPC
        interprets physical layer signals on CC lines and Vbus, and sends that
        information to the TCPM to decide what action to take. In older designs,
        there was a separate EC (running this codebase) that acted as the TCPC
        that communicated with the main EC (also running this codebase), which
        acted as the TCPM. More info in the official
        [TCPC spec][USB TCPM Spec Id].
*   TCPM {#tcpm}
    *   Type-C Port Manager. Manages the state of the USB-C connection. Makes
        decisions about what state to transition to. This is the code running on
        the EC itself.
*   PE {#pe}
    *   Policy Engine. According to the [TypeC spec][USB TC Spec Id], the policy
        engine is the state machine that decides how the USB-C connection
        progresses through different states and which USB-C PD features are
        available, such as Try.SRC
*   TC {#tc}
    *   Type-C physical layer.
*   PPC {#ppc}
    *   Power Path Controller. An optional, separate IC that isolates various
        USB-C signals from each other and the rest of the board. This IC should
        prevent shorts and over current/voltage scenarios for Vbus. Some PPCs
        will protect signals other than Vbus as well.
*   SSMUX {#ssmux}
    *   SuperSpeed Mux. This is typically the same IC as the TCPC; it enables
        the mirrored orientation of the USB-C cable to go to the correct pins on
        SoC. Also, allows the SuperSpeed signal to be used for different
        purposes, such as USB data or DisplayPort.
*   SVDM {#svdm}
    *   Structured Vendor Defined Messages are a class of [USB PD](#pd) messages
        to enable non-power related communication between port partners. SVDMs
        are used to negotiate and set the display port mode on a USB-C
        connection.
*   DRP {#drp}
    *   Dual Role Power Port. A USB-C port that can act as either a power Source
        or power Sink.
*   UFP {#ufp}
    *   Upstream Facing Port. The USB data role that is typical for a peripheral
        (e.g. HID keyboard).
*   DFP {#dfp}
    *   Downstream Facing Port. The USB Data role that is typical for a host
        machine (e.g. device running ChromeOS).

*   E-Mark {#emark}
    *   Electronically marked cable. A USB-C cable that contains an embedded
        chip in the cable, used to identify the capabilities of the cable.

*   VCONN {#vconn}
    *   Connector Voltage. A dedicated power supply rail for [E-Mark](#emark)
        cables and other accessory functions (such as display dongles, and
        docks). VCONN re-uses one of the CC1/CC2 signals to provide 5 volt, 1
        watt, of power.

## Different PD stacks

Right now platform/ec has two different implementations of USB-C PD stack.

1.  The older implementation is mainly contained within
    [`usb_pd_protocol.c`](../common/usb_pd_protocol.c) and
    [`usb_pd_policy.c`](../common/usb_pd_policy.c)
2.  The newer implementation is found under [`common/usbc`](../common/usbc) and
    is broken up into multiple different files and state machines
    *   Policy engine state machine files, `usb_pe_*_sm.c`.
    *   Protocol engine state machine file, `usb_prl_*_sm.c`.
    *   State machine framework file, `usb_sm.c`.
    *   Type-C physical layer state machine files, `usb_tc_*_sm.c`.
    *   USB-C PD Task file, `usbc_task.c`.

The older implementation supports firmware for device types other than
Chromebooks. For example, the older stack supports the Zinger, which is the
USB-C charging device that shipped with Samus, the Google Chromebook Pixel 2.
The Zinger implements the charger only side of the USB PD protocol.

To use the newer USB-C PD stack implementation, see
[TCPMv2 Overview](usb-tcpmv2.md).

## Implementation Considerations

In both older and newer implementations, the following details apply:

*   For each USB-C port, there must be two tasks: `PD_C#` and `PD_INT_C#`, where
    `#` is the port number starting from `0`.
    *   The `PD_C#` task runs the state machine (old or new) for the port and
        communicates with the TCPC, MUX, and PPC. This task needs a large task
        stack.
    *   The `PD_INT_C#` tasks run at a higher priority than the state machine
        task, and its sole job is to receive interrupts from the TCPC as quickly
        as possible then send appropriate messages to other tasks (including
        `PD_C#`). This task shouldn't need much stack space, but the i2c
        recovery code requires a decent amount of stack space so it ends up
        needing a fair amount too.
*   Saving PD state between EC jumps
    *   PD communication is disabled in locked RO images (normal state for
        customer devices). When the jump from RO to RW happens relatively
        quickly (e.g. there is not a long memory training step), then there
        aren't many problems when RW takes over and negotiates higher PD
        contracts.
    *   To support factory use cases that don't have a battery (and are
        therefore unlocked), PD communication is enabled in unlocked RO. This
        allows systems without software sync enabled to get a higher power
        contract than 15W in RO.
    *   We save and restore PD state between RO -> RW and RW -> RO jump to allow
        us to maintain a higher negotiated power through the full jump and
        re-initialization process. For example, for each port we save the power
        role, data role, and Vconn sourcing state in battery-backed or
        non-volatile RAM. This allows the firmware image that is initializing to
        restore an existing SNK contract (Chromebook as SNK) without cutting
        power. We don't cut the power from the external supplier because we
        issue a SoftReset (leaves Vbus intact) instead of a HardReset (drops
        Vbus) in this contract resume case.
    *   Both use cases where we actually are able to restore the PD contract
        require an unlocked RO (e.g. factory) otherwise RO cannot communicate
        via PD and will drop the higher PD contract (by applying Rp/Rp on the CC
        lines temporarily)
        *   The RO->RW use case is for an unlocked (e.g. factory) device that
            negotiated power and we want to keep that contract after we jump to
            RW in the normal software sync boot process. This is especially
            useful when there is no battery and Vbus is our only power source.
        *   The RW->RO use case happens when we are performing auxiliary FW
            upgrades during software sync and BIOS instructs the EC to jump back
            to RO. We'll also try to maintain contracts over an EC reset when
            unlocked.

## Configuration

There are many `CONFIG_*` options and driver structs that are needed in the
board.h and board.c implementation.

### TCPC Config

The `tcpc_config` array of `tcpc_config_t` structs defined in `board.c` (or
baseboard equivalent) should be defined for every board. The index in the
`tcpc_config` array corresponds to the USB-C port number. This struct should
point to the specific TCPC driver that corresponds to the TCPC that is being
used on that port. The i2c port and address for the TCPC are also specified
here.

### SSMUX Config

The `usb_muxes` array of `usb_mux` structs defined in `board.c` (or baseboard
equivalent) should be defined for every board. Normally the standard
`tcpci_tcpm_usb_mux_driver` driver works, especially if TCPC and MUX are the
same IC.

If the signal strength for the high-speed data lines needs to be tuned for a
specific hardware layout, the `board_init` field on the `usb_mux` is called
every time the mux is woken up from a low power state and should be used for
setting custom board tuning parameters.

### PPC Config

Some boards have an additional IC that sits between the physical USB-C connector
and the rest of the board. The PPC IC gates whether the Vbus line is an input or
output signal, based on i2c settings or gpio pins. A PPC also typically provides
over voltage and over current protection on multiple USB-C pins.

The `ppc_chips` array of `ppc_config_t` structs defined in `board.c` (or
baseboard equivalent) sets the appropriate driver and i2c port/address for the
PPC IC.

### Useful Config Options

Many USB-C policies and features are gated by various `CONFIG_*` options that
should be defined in `board.h` (or baseboard equivalent).

Most USB-C options will start with `CONFIG_USB_PD_` or `CONFIG_USBC_`. For their
full descriptions see [config.h][config header link]

## Interactions with other tasks

TODO(https://crbug.com/974302): mention `USB_CHG_P#` and `CHARGER`

## Upgrading FW for TCPCs

TODO(https://crbug.com/974302): Mention how this works even though it is in depthcharge.
Probing now. Need new driver in depthcharge

[Case Closed Debugging (CCD)]: https://chromium.googlesource.com/chromiumos/platform/ec/+/cr50_stab/docs/case_closed_debugging_cr50.md
[Introduction to USB Power Delivery]: https://www.microchip.com/wwwAppNotes/AppNotes.aspx?appnote=en575003
[USB PD Spec Id]: https://www.usb.org/document-library/usb-power-delivery
[USB TC Spec Id]: https://www.usb.org/document-library/usb-type-cr-cable-and-connector-specification-revision-20-august-2019
[USB TCPM Spec Id]: https://www.usb.org/document-library/usb-type-ctm-port-controller-interface-specification
[config header link]: ../include/config.h
