# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Configure-time checks for the named-gpios node."""

import argparse
import inspect
import logging
from pathlib import Path
import pickle
import site
import sys
from typing import List, Optional


def verify_no_duplicates(zephyr_base, edt_pickle):
    """Verify there are no duplicate GPIOs in the named-gpios node.

    Args:
        zephyr_base: pathlib.Path pointing to the Zephyr OS repository.
        edt_pickle: pathlib.Path pointing to the EDT object, stored as a pickle
            file.

    Returns:
        True if no duplicates found.  Returns False otherwise.
    """
    zephyr_devicetree_path = (
        zephyr_base / "scripts" / "dts" / "python-devicetree" / "src"
    )

    # Add Zephyr's python-devicetree into the source path.
    site.addsitedir(zephyr_devicetree_path)

    try:
        with open(edt_pickle, "rb") as edt_file:
            edt = pickle.load(edt_file)
    except FileNotFoundError:
        # Skip the GPIOs check if the edt_pickle file doesn't exist.
        # UnpicklingErrors will generate a failure.
        return True

    # Dictionary of GPIO controllers, indexed by the GPIO controller nodelabel
    gpio_ctrls = dict()
    duplicates = 0
    count = 0

    edtlib = inspect.getmodule(edt)

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
        logging.error("%d duplicate GPIOs found in %s", duplicates, edt_pickle)
        return False

    logging.info("Verified %d GPIOs, no duplicates found", count)
    return True


# Dictionary used to map log level strings to their corresponding int values.
log_level_map = {
    "DEBUG": logging.DEBUG,
    "INFO": logging.INFO,
    "WARNING": logging.WARNING,
    "ERROR": logging.ERROR,
    "CRITICAL": logging.CRITICAL,
}


def parse_args(argv: Optional[List[str]] = None):
    """Returns parsed command-line arguments"""
    parser = argparse.ArgumentParser(
        prog="named_gpios",
        description="Zephyr EC specific devicetree checks",
    )

    parser.add_argument(
        "--zephyr-base",
        type=Path,
        help="Path to Zephyr OS repository",
        required=True,
    )

    parser.add_argument(
        "--edt-pickle",
        type=Path,
        help="EDT object file, in pickle format",
        required=True,
    )

    parser.add_argument(
        "-l",
        "--log-level",
        choices=log_level_map.values(),
        metavar=f"{{{','.join(log_level_map)}}}",
        type=lambda x: log_level_map[x],
        default=logging.INFO,
        help="Set the logging level (default=INFO)",
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

    if not verify_no_duplicates(args.zephyr_base, args.edt_pickle):
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
