# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Configure-time checks for the named-gpios node."""

import logging
import sys
from typing import List, Optional

from scripts import util


def _detect_gpios_mismatches(node_name, prop_name, prop_gpios, board_gpios):
    """Verify that all GPIO entries in a -gpios style property match the flags
    specified in the named-gpios node.

    Args:
        node_name   - The name of the node containing the -gpios style property.
        prop_name   - The name of the -gpios style property.
        prop_gpios  - An edtlib.ControllerAndData class object containing
                      the GPIO tuples found in the property.
        board_gpios - A dictionary that maps gpio port/pin tuples to the GPIO
                      flags. This dictionary is initialized from the children
                      found in the named-gpios devicetree node.

    Returns:
        A tuple indicating how many GPIOs were checked, and how many GPIOs
        had a mismatch.
    """
    errors = 0
    count = 0

    # The -gpios property may be an array of GPIO tuples
    for gpio in prop_gpios:
        count += 1
        gpio_pin = gpio.data["pin"]

        # The "flags" cell should be only 16-bits
        dt_flags = gpio.data["flags"] & 0xFFFF

        if gpio.controller.labels[0] is not None:
            nodelabel = gpio.controller.labels[0]
        else:
            nodelabel = gpio.controller.name

        if (nodelabel, gpio_pin) not in board_gpios.keys():
            # GPIO not specified in named-gpios
            logging.debug(
                "Warning: property %s/%s = <%s %s 0x%x> not found in named-gpios",
                node_name,
                prop_name,
                nodelabel,
                gpio_pin,
                dt_flags,
            )
            continue

        if dt_flags != board_gpios[(nodelabel, gpio_pin)]:
            errors += 1
            logging.error(
                "ERROR: property %s/%s = <%s %s 0x%x>. Flags don't match named-gpios 0x%x",
                node_name,
                prop_name,
                nodelabel,
                gpio_pin,
                dt_flags,
                board_gpios[(nodelabel, gpio_pin)],
            )

    return count, errors


def verify_gpios_flags_match(edtlib, edt, project_name):
    """Check that GPIO flags used across devices matches.

    Until all drivers are upstream, the Zephyr EC devicetrees specify GPIOs
    in multiple locations: the "named-gpios" node performs the configuration
    of all GPIOs on the board, and individual drivers may also configure GPIOs.

    This routine finds all "*-gpios" style properties in the devicetree and
    cross-checks the flags set by "named-gpios" and ensures they match.

    Args:
        edtlib: Module object for the edtlib library.
        edt: EDT object representation of a devicetree
        project_name: A string containing the project/test name

    Returns:
        True if no GPIO flag mismatches found.  Returns False otherwise.
    """
    try:
        named_gpios = edt.get_node("/named-gpios")
    except edtlib.EDTError:
        # If the named-gpios node doesn't exist, return success.
        return True

    # Dictionary using the gpio,pin tuple as the key. Value set to the flags
    board_gpios = dict()
    for node in named_gpios.children.values():
        if "gpios" not in node.props:
            continue

        gpios = node.props["gpios"].val

        # edtlib converts a "-gpios" style property to a list of of
        # ControllerAndData objects.  However, the named-gpios node only
        # supports a single GPIO per child, so no need to iterate over
        # the list.
        gpio = gpios[0]
        gpio_pin = gpio.data["pin"]

        if gpio.controller.labels[0] is not None:
            nodelabel = gpio.controller.labels[0]
        else:
            nodelabel = gpio.controller.name

        # The named-gpios stores a 32-bit flags, but Zephyr GPIO flags
        # are limited to the lower 16-bits
        board_gpios[(nodelabel, gpio_pin)] = gpio.data["flags"] & 0xFFFF

    errors = 0
    count = 0
    for node in edt.nodes:
        for prop_name in node.props.keys():
            if prop_name.endswith("-gpios"):
                gpios = node.props[prop_name].val

                prop_gpio_count, prop_gpio_errors = _detect_gpios_mismatches(
                    node.name, prop_name, gpios, board_gpios
                )
                count += prop_gpio_count
                errors += prop_gpio_errors

    if errors:
        logging.error("%d GPIO mismatches found in %s.", errors, project_name)
        return False

    logging.info("Verified %d '*-gpios' properties, all flags match", count)
    return True


def verify_no_duplicates(edtlib, edt, project_name):
    """Verify there are no duplicate GPIOs in the named-gpios node.

    Args:
        edtlib: Module object for the edtlib library.
        edt: EDT object representation of a devicetree
        project_name: A string containing the project/test name

    Returns:
        True if no duplicates found.  Returns False otherwise.
    """
    # Dictionary of GPIO controllers, indexed by the GPIO controller nodelabel
    gpio_ctrls = dict()
    duplicates = 0
    count = 0

    try:
        named_gpios = edt.get_node("/named-gpios")
    except edtlib.EDTError:
        # If the named-gpios node doesn't exist, return success.
        return True

    for node in named_gpios.children.values():
        if "gpios" not in node.props:
            continue

        gpios = node.props["gpios"].val
        count += 1

        # edtlib converts a "-gpios" style property to a list of of
        # ControllerAndData objects.  However, the named-gpios node only
        # supports a single GPIO per child, so no need to iterate over
        # the list.
        gpio = gpios[0]
        gpio_pin = gpio.data["pin"]

        # Note that EDT stores the node name (not a nodelabel) in the
        # Node.name property.  Use the nodelabel, if available as the
        # key for gpio_ctrls.
        if gpio.controller.labels[0] is not None:
            nodelabel = gpio.controller.labels[0]
        else:
            nodelabel = gpio.controller.name

        if not nodelabel in gpio_ctrls:
            # Create a dictionary at each GPIO controller
            gpio_ctrls[nodelabel] = dict()

        if gpio_pin in gpio_ctrls[nodelabel]:
            logging.error(
                "Duplicate GPIOs found at nodes: %s and %s",
                gpio_ctrls[nodelabel][gpio_pin],
                node.name,
            )
            duplicates += 1
        else:
            # Store the node name for the new pin
            gpio_ctrls[nodelabel][gpio_pin] = node.name

    if duplicates:
        logging.error(
            "%d duplicate GPIOs found in %s", duplicates, project_name
        )
        return False

    logging.info("Verified %d GPIOs, no duplicates found", count)
    return True


def parse_args(argv: Optional[List[str]] = None):
    """Returns parsed command-line arguments"""
    parser = util.EdtArgumentParser(
        prog="named_gpios",
        description="Zephyr EC specific devicetree checks",
    )

    return parser.parse_args(argv)


def main(argv: Optional[List[str]] = None) -> Optional[int]:
    """The main function.

    Args:
        argv: Optionally, the command-line to parse, not including argv[0].

    Returns:
        Zero upon success, or non-zero upon failure.
    """
    args = parse_args(argv)

    log_format = "%(levelname)s: %(message)s"

    logging.basicConfig(format=log_format, level=args.log_level)

    edtlib, edt, project_dir = util.load_edt(args.zephyr_base, args.edt_pickle)

    if edtlib is None:
        return 0

    if not verify_no_duplicates(edtlib, edt, project_dir.name):
        return 1

    if not verify_gpios_flags_match(edtlib, edt, project_dir.name):
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
