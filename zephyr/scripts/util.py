# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"Util functions for other scripts"

import argparse
import inspect
import logging
from pathlib import Path
import pickle
import re
import site


def load_edt(zephyr_base, edt_pickle):
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

    is_test = re.compile(r"twister-out")

    if is_test.search(edt_pickle.as_posix()):
        # For tests built with twister, the edt.pickle file is located in a
        # path ending <test_name>/zephyr/.
        project_name = edt_pickle.parents[1].name
    else:
        # For Zephyr EC project, the edt.pickle file is located in a path
        # ending <project>/build-[ro|rw|single-image]/zephyr/.
        project_name = edt_pickle.parents[2].name

    edtlib = inspect.getmodule(edt)

    return edtlib, edt, project_name


class EdtArgumentParser(argparse.ArgumentParser):
    """Argument parser class that adds all default zephyr arguments"""

    def __init__(self, *args, **kwargs):
        super(EdtArgumentParser, self).__init__(*args, **kwargs)

        self.add_argument(
            "--zephyr-base",
            type=Path,
            help="Path to Zephyr OS repository",
            required=True,
        )

        self.add_argument(
            "--edt-pickle",
            type=Path,
            help="EDT object file, in pickle format",
            required=True,
        )

        # Dictionary used to map log level strings to their corresponding int values.
        log_level_map = {
            "DEBUG": logging.DEBUG,
            "INFO": logging.INFO,
            "WARNING": logging.WARNING,
            "ERROR": logging.ERROR,
            "CRITICAL": logging.CRITICAL,
        }

        self.add_argument(
            "-l",
            "--log-level",
            choices=log_level_map.values(),
            metavar=f"{{{','.join(log_level_map)}}}",
            type=lambda x: log_level_map[x],
            default=logging.INFO,
            help="Set the logging level (default=INFO)",
        )
