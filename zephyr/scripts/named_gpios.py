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


def _load_edt(zephyr_base, edt_pickle):
    """Load an EDT object from a pickle file source.

    Args:
        zephyr_base: pathlib.Path pointing to the Zephyr OS repository.
        edt_pickle: pathlib.Path pointing to the EDT object, stored as a pickle
            file.

    Returns:
        A 3-field tuple: (edtlib, edt, project_name)
            edtlib: module object for the edtlib
            edt: EDT object of the devicetree
            project_name: string containing the name of the project or test.

        Returns None if the edtlib pickle file doesn't exist.
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
        # Skip the all EC specific checks if the edt_pickle file doesn't exist.
        # UnpicklingErrors will raise an exception and fail the build.
        return None, None, None

    if "twister-out" in edt_pickle.parts:
        # For tests built with twister, the edt.pickle file is located in a
        # path ending <test_name>/zephyr/.
        project_name = edt_pickle.parents[1].name
    else:
        # For Zephyr EC project, the edt.pickle file is located in a path
        # ending <project>/build-[ro|rw|single-image]/zephyr/.
        project_name = edt_pickle.parents[2].name

    edtlib = inspect.getmodule(edt)

    return edtlib, edt, project_name


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

    edtlib, edt, project_name = _load_edt(args.zephyr_base, args.edt_pickle)

    if edtlib is None:
        return 0

    if not verify_no_duplicates(edtlib, edt, project_name):
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
