# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Entry point for Bazel execution.

Bazel can't use zmake/__main__.py as it puts the main in PYTHONPATH.
"""

import sys

from zmake import __main__


if __name__ == "__main__":
    sys.exit(__main__.main())
