# Copyright 2018 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for powerlog."""

import os
import shutil
import tempfile
import unittest

from usb_power import powerlog


class TestPowerlog(unittest.TestCase):
    """Test to verify powerlog util methods work as expected."""

    def setUp(self):
        """Set up data and create a temporary directory to save data and stats."""
        self.tempdir = tempfile.mkdtemp()
        self.filename = "testfile"
        self.filepath = os.path.join(self.tempdir, self.filename)
        with open(self.filepath, "w", encoding="utf-8") as fff:
            fff.write("")

    def tearDown(self):
        """Delete the temporary directory and its content."""
        shutil.rmtree(self.tempdir)

    def test_ProcessFilenameAbsoluteFilePath(
        self,
    ):  # pylint:disable=invalid-name
        """Absolute file path is returned unchanged."""
        processed_fname = powerlog.process_filename(self.filepath)
        self.assertEqual(self.filepath, processed_fname)

    def test_ProcessFilenameRelativeFilePath(
        self,
    ):  # pylint:disable=invalid-name
        """Finds relative file path inside a known config location."""
        original = powerlog.CONFIG_LOCATIONS
        powerlog.CONFIG_LOCATIONS = [self.tempdir]
        processed_fname = powerlog.process_filename(self.filename)
        try:
            self.assertEqual(self.filepath, processed_fname)
        finally:
            powerlog.CONFIG_LOCATIONS = original

    def test_ProcessFilenameInvalid(self):  # pylint:disable=invalid-name
        """IOError is raised when file cannot be found by any of the four ways."""
        with self.assertRaises(IOError):
            powerlog.process_filename(self.filename)


if __name__ == "__main__":
    unittest.main()
