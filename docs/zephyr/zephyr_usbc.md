# Zephyr EC USBC Configuration

[TOC]

## Overview

[USB-C] is a flexible connector supporting multiple data rates, protocols, and
power in either direction.

From the system, USB PD requires a complex state machine as USB PD can
operate in many different modes. This includes but isn't limited to:

*   Negotiated power contracts. Either side of the cable can source or sink
    power up to 240W (if supported by device).
*   Reversed cable mode. This requires a mux to switch the signals before
    getting to the SoC (or AP).
*   Debug accessory mode, e.g. [Case Closed Debugging (CCD)]
*   Multiple uses for the 4 differential pair signals including
    *   USB SuperSpeed mode (up to 4 lanes for USB data)
    *   DisplayPort Alternate Mode (up to 4 lanes for DisplayPort data)
    *   Dock Mode (2 lanes for USB data, and 2 lanes for DisplayPort)
    *   USB4/Thunderbolt mode. (4 lanes for USB data)

For a more complete list of USB-C Power Delivery features, see the
[USB-C PD spec][USB PD Spec Id].

The image below shows a block diagram of a typical [USB-C] setup.

![USBC Block Diagram]
# Legend
## See the [EC Acronyms and Technologies]
*   [AP]
*   [EC]
*   [PPC]
*   [TCPC]
*   [USB-C Mux]
*   [Retimer]

## Kconfig Options

The `CONFIG_PLATFORM_EC_USBC` option enables USB-C support on the Chromebook.
See [Kconfig.usbc] for sub-options related to this feature.

The following sub-components must be configured for correct operation of USB-C:
*   [Zephyr EC Type-C Port Controller]
*   [Zephyr EC PPC Configuration]
*   [Zephyr EC SS Mux and Retimer Configuration]
*   [Zephyr EC Charger Configuration]
*   [Zephyr EC Power Delivery Configuration]

[USB-C]:../usb-c.md
[EC Acronyms and Technologies]:../ec_terms.md
[AP]:../ec_terms.md#ap
[EC]:../ec_terms.md#ec
[PPC]:../ec_terms.md#ppc
[TCPC]:../ec_terms.md#tcpc
[USB-C Mux]:../ec_terms.md#ssmux
[Retimer]:../ec_terms.md#retimer
[USBC Block Diagram]:../images/usbc_block_diagram.png
[USB PD Spec Id]: https://www.usb.org/document-library/usb-power-delivery
[Kconfig.usbc]:
https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/zephyr/Kconfig.usbc
[Zephyr EC Power Delivery Configuration]:zephyr_pd.md
[Zephyr EC PPC Configuration]:zephyr_ppc.md
[Zephyr EC SS Mux and Retimer Configuration]:zephyr_usbc_ss_mux_and_retimer.md
[Zephyr EC Charger Configuration]:zephyr_charger.md
[Zephyr EC Type-C Port Controller]:zephyr_tcpc.md
