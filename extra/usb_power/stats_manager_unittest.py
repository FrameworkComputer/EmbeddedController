# Copyright 2017 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for StatsManager."""

import json
import os
import re
import shutil
import tempfile
import unittest

import stats_manager  # pylint:disable=import-error


class TestStatsManager(
    unittest.TestCase
):  # pylint:disable=too-many-public-methods
    """Test to verify StatsManager methods work as expected.

    StatsManager should collect raw data, calculate their statistics, and save
    them in expected format.
    """

    def _populate_mock_stats(self):
        """Create a populated & processed StatsManager to test data retrieval."""
        self.data.AddSample("A", 99999.5)
        self.data.AddSample("A", 100000.5)
        self.data.SetUnit("A", "uW")
        self.data.SetUnit("A", "mW")
        self.data.AddSample("B", 1.5)
        self.data.AddSample("B", 2.5)
        self.data.AddSample("B", 3.5)
        self.data.SetUnit("B", "mV")
        self.data.CalculateStats()

    def _populate_mock_stats_no_unit(self):
        self.data.AddSample("B", 1000)
        self.data.AddSample("A", 200)
        self.data.SetUnit("A", "blue")

    def setUp(self):
        """Set up StatsManager and create a temporary directory for test."""
        self.tempdir = tempfile.mkdtemp()
        self.data = stats_manager.StatsManager()

    def tearDown(self):
        """Delete the temporary directory and its content."""
        shutil.rmtree(self.tempdir)

    def test_AddSample(self):  # pylint:disable=invalid-name
        """Adding a sample successfully adds a sample."""
        self.data.AddSample("Test", 1000)
        self.data.SetUnit("Test", "test")
        self.data.CalculateStats()
        summary = self.data.GetSummary()
        self.assertEqual(1, summary["Test"]["count"])

    def test_AddSampleNoFloatAcceptNaN(self):  # pylint:disable=invalid-name
        """Adding a non-number adds 'NaN' and doesn't raise an exception."""
        self.data.AddSample("Test", 10)
        self.data.AddSample("Test", 20)
        # adding a fake NaN: one that gets converted into NaN internally
        self.data.AddSample("Test", "fiesta")
        # adding a real NaN
        self.data.AddSample("Test", float("NaN"))
        self.data.SetUnit("Test", "test")
        self.data.CalculateStats()
        summary = self.data.GetSummary()
        # assert that 'NaN' as added.
        self.assertEqual(4, summary["Test"]["count"])
        # assert that mean, min, and max calculatings ignore the 'NaN'
        self.assertEqual(10, summary["Test"]["min"])
        self.assertEqual(20, summary["Test"]["max"])
        self.assertEqual(15, summary["Test"]["mean"])

    def test_AddSampleNoFloatNotAcceptNaN(self):  # pylint:disable=invalid-name
        """Adding a non-number raises a StatsManagerError if accept_nan is False."""
        self.data = stats_manager.StatsManager(accept_nan=False)
        with self.assertRaisesRegex(
            stats_manager.StatsManagerError,
            "accept_nan is false. Cannot add NaN sample.",
        ):
            # adding a fake NaN: one that gets converted into NaN internally
            self.data.AddSample("Test", "fiesta")
        with self.assertRaisesRegex(
            stats_manager.StatsManagerError,
            "accept_nan is false. Cannot add NaN sample.",
        ):
            # adding a real NaN
            self.data.AddSample("Test", float("NaN"))

    def test_AddSampleNoUnit(self):  # pylint:disable=invalid-name
        """Not adding a unit does not cause an exception on CalculateStats()."""
        self.data.AddSample("Test", 17)
        self.data.CalculateStats()
        summary = self.data.GetSummary()
        self.assertEqual(1, summary["Test"]["count"])

    def test_UnitSuffix(self):  # pylint:disable=invalid-name
        """Unit gets appended as a suffix in the displayed summary."""
        self.data.AddSample("test", 250)
        self.data.SetUnit("test", "mw")
        self.data.CalculateStats()
        summary_str = self.data.SummaryToString()
        self.assertIn("test_mw", summary_str)

    def test_DoubleUnitSuffix(self):  # pylint:disable=invalid-name
        """If domain already ends in unit, verify that unit doesn't get appended."""
        self.data.AddSample("test_mw", 250)
        self.data.SetUnit("test_mw", "mw")
        self.data.CalculateStats()
        summary_str = self.data.SummaryToString()
        self.assertIn("test_mw", summary_str)
        self.assertNotIn("test_mw_mw", summary_str)

    def test_GetRawData(self):  # pylint:disable=invalid-name
        """GetRawData returns exact same data as fed in."""
        self._populate_mock_stats()
        raw_data = self.data.GetRawData()
        self.assertListEqual([99999.5, 100000.5], raw_data["A"])
        self.assertListEqual([1.5, 2.5, 3.5], raw_data["B"])

    def test_GetSummary(self):  # pylint:disable=invalid-name
        """GetSummary returns expected stats about the data fed in."""
        self._populate_mock_stats()
        summary = self.data.GetSummary()
        self.assertEqual(2, summary["A"]["count"])
        self.assertAlmostEqual(100000.5, summary["A"]["max"])
        self.assertAlmostEqual(99999.5, summary["A"]["min"])
        self.assertAlmostEqual(0.5, summary["A"]["stddev"])
        self.assertAlmostEqual(100000.0, summary["A"]["mean"])
        self.assertEqual(3, summary["B"]["count"])
        self.assertAlmostEqual(3.5, summary["B"]["max"])
        self.assertAlmostEqual(1.5, summary["B"]["min"])
        self.assertAlmostEqual(0.81649658092773, summary["B"]["stddev"])
        self.assertAlmostEqual(2.5, summary["B"]["mean"])

    def test_SaveRawData(self):  # pylint:disable=invalid-name
        """SaveRawData stores same data as fed in."""
        self._populate_mock_stats()
        dirname = "unittest_raw_data"
        expected_files = set(["A_mW.txt", "B_mV.txt"])
        fnames = self.data.SaveRawData(self.tempdir, dirname)
        files_returned = {os.path.basename(f) for f in fnames}
        # Assert that only the expected files got returned.
        self.assertEqual(expected_files, files_returned)
        # Assert that only the returned files are in the outdir.
        self.assertEqual(
            set(os.listdir(os.path.join(self.tempdir, dirname))), files_returned
        )
        for fname in fnames:
            with open(fname, "r", encoding="utf-8") as fff:
                if "A_mW" in fname:
                    self.assertEqual("99999.50", fff.readline().strip())
                    self.assertEqual("100000.50", fff.readline().strip())
                if "B_mV" in fname:
                    self.assertEqual("1.50", fff.readline().strip())
                    self.assertEqual("2.50", fff.readline().strip())
                    self.assertEqual("3.50", fff.readline().strip())

    def test_SaveRawDataNoUnit(self):  # pylint:disable=invalid-name
        """SaveRawData appends no unit suffix if the unit is not specified."""
        self._populate_mock_stats_no_unit()
        self.data.CalculateStats()
        outdir = "unittest_raw_data"
        files = self.data.SaveRawData(self.tempdir, outdir)
        files = [os.path.basename(f) for f in files]
        # Verify nothing gets appended to domain for filename if no unit exists.
        self.assertIn("B.txt", files)

    def test_SaveRawDataSMID(self):  # pylint:disable=invalid-name
        """SaveRawData uses the smid when creating output filename."""
        identifier = "ec"
        self.data = stats_manager.StatsManager(smid=identifier)
        self._populate_mock_stats()
        files = self.data.SaveRawData(self.tempdir)
        for fname in files:
            self.assertTrue(os.path.basename(fname).startswith(identifier))

    def test_SummaryToStringNaNHelp(self):  # pylint:disable=invalid-name
        """NaN containing row gets tagged with *, help banner gets added."""
        help_banner_exp = (
            f"{stats_manager.STATS_PREFIX} {stats_manager.NAN_DESCRIPTION}"
        )
        nan_domain = "A-domain"
        nan_domain_exp = "{nan_domain}{stats_manager.NAN_TAG}"
        # NaN helper banner is added when a NaN domain is found & domain gets tagged
        data = stats_manager.StatsManager()
        data.AddSample(nan_domain, float("NaN"))
        data.AddSample(nan_domain, 17)
        data.AddSample("B-domain", 17)
        data.CalculateStats()
        summarystr = data.SummaryToString()
        self.assertIn(help_banner_exp, summarystr)
        self.assertIn(nan_domain_exp, summarystr)
        # NaN helper banner is not added when no NaN domain output, no tagging
        data = stats_manager.StatsManager()
        # nan_domain in this scenario does not contain any NaN
        data.AddSample(nan_domain, 19)
        data.AddSample("B-domain", 17)
        data.CalculateStats()
        summarystr = data.SummaryToString()
        self.assertNotIn(help_banner_exp, summarystr)
        self.assertNotIn(nan_domain_exp, summarystr)

    def test_SummaryToStringTitle(self):  # pylint:disable=invalid-name
        """Title shows up in SummaryToString if title specified."""
        title = "titulo"
        data = stats_manager.StatsManager(title=title)
        self._populate_mock_stats()
        summary_str = data.SummaryToString()
        self.assertIn(title, summary_str)

    def test_SummaryToStringHideDomains(self):  # pylint:disable=invalid-name
        """Keys indicated in hide_domains are not printed in the summary."""
        data = stats_manager.StatsManager(hide_domains=["A-domain"])
        data.AddSample("A-domain", 17)
        data.AddSample("B-domain", 17)
        data.CalculateStats()
        summary_str = data.SummaryToString()
        self.assertIn("B-domain", summary_str)
        self.assertNotIn("A-domain", summary_str)

    def test_SummaryToStringOrder(self):  # pylint:disable=invalid-name
        """Order passed into StatsManager is honoured when formatting summary."""
        # StatsManager that should print D & B first, and the subsequent elements
        # are sorted.
        d_b_a_c_regexp = re.compile(
            "D-domain.*B-domain.*A-domain.*C-domain", re.DOTALL
        )
        data = stats_manager.StatsManager(order=["D-domain", "B-domain"])
        data.AddSample("A-domain", 17)
        data.AddSample("B-domain", 17)
        data.AddSample("C-domain", 17)
        data.AddSample("D-domain", 17)
        data.CalculateStats()
        summary_str = data.SummaryToString()
        self.assertRegex(summary_str, d_b_a_c_regexp)

    def test_MakeUniqueFName(self):  # pylint:disable=invalid-name
        """Test of _MakeUniqueFName function."""
        data = stats_manager.StatsManager()
        testfile = os.path.join(self.tempdir, "testfile.txt")
        with open(testfile, "w", encoding="utf-8") as fff:
            fff.write("")
        expected_fname = os.path.join(self.tempdir, "testfile0.txt")
        self.assertEqual(
            expected_fname,
            data._MakeUniqueFName(testfile),  # pylint:disable=protected-access
        )

    def test_SaveSummary(self):  # pylint:disable=invalid-name
        """SaveSummary properly dumps the summary into a file."""
        self._populate_mock_stats()
        fname = "unittest_summary.txt"
        expected_fname = os.path.join(self.tempdir, fname)
        fname = self.data.SaveSummary(self.tempdir, fname)
        # Assert the reported fname is the same as the expected fname
        self.assertEqual(expected_fname, fname)
        # Assert only the reported fname is output (in the tempdir)
        self.assertEqual(
            set([os.path.basename(fname)]), set(os.listdir(self.tempdir))
        )
        with open(fname, "r", encoding="utf-8") as fff:
            self.assertEqual(
                "@@   NAME  COUNT       MEAN  STDDEV        MAX       MIN\n",
                fff.readline(),
            )
            self.assertEqual(
                "@@   A_mW      2  100000.00    0.50  100000.50  99999.50\n",
                fff.readline(),
            )
            self.assertEqual(
                "@@   B_mV      3       2.50    0.82       3.50      1.50\n",
                fff.readline(),
            )

    def test_SaveSummarySMID(self):  # pylint:disable=invalid-name
        """SaveSummary uses the smid when creating output filename."""
        identifier = "ec"
        self.data = stats_manager.StatsManager(smid=identifier)
        self._populate_mock_stats()
        fname = os.path.basename(self.data.SaveSummary(self.tempdir))
        self.assertTrue(fname.startswith(identifier))

    def test_SaveSummaryJSON(self):  # pylint:disable=invalid-name
        """SaveSummaryJSON saves the added data properly in JSON format."""
        self._populate_mock_stats()
        fname = "unittest_summary.json"
        expected_fname = os.path.join(self.tempdir, fname)
        fname = self.data.SaveSummaryJSON(self.tempdir, fname)
        # Assert the reported fname is the same as the expected fname
        self.assertEqual(expected_fname, fname)
        # Assert only the reported fname is output (in the tempdir)
        self.assertEqual(
            set([os.path.basename(fname)]), set(os.listdir(self.tempdir))
        )
        with open(fname, "r", encoding="utf-8") as fff:
            summary = json.load(fff)
            self.assertAlmostEqual(100000.0, summary["A"]["mean"])
            self.assertEqual("milliwatt", summary["A"]["unit"])
            self.assertAlmostEqual(2.5, summary["B"]["mean"])
            self.assertEqual("millivolt", summary["B"]["unit"])

    def test_SaveSummaryJSONSMID(self):  # pylint:disable=invalid-name
        """SaveSummaryJSON uses the smid when creating output filename."""
        identifier = "ec"
        self.data = stats_manager.StatsManager(smid=identifier)
        self._populate_mock_stats()
        fname = os.path.basename(self.data.SaveSummaryJSON(self.tempdir))
        self.assertTrue(fname.startswith(identifier))

    def test_SaveSummaryJSONNoUnit(self):  # pylint:disable=invalid-name
        """SaveSummaryJSON marks unknown units properly as N/A."""
        self._populate_mock_stats_no_unit()
        self.data.CalculateStats()
        fname = "unittest_summary.json"
        fname = self.data.SaveSummaryJSON(self.tempdir, fname)
        with open(fname, "r", encoding="utf-8") as fff:
            summary = json.load(fff)
            self.assertEqual("blue", summary["A"]["unit"])
            # if no unit is specified, JSON should save 'N/A' as the unit.
            self.assertEqual("N/A", summary["B"]["unit"])


if __name__ == "__main__":
    unittest.main()
