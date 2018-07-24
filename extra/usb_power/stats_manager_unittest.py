# Copyright 2017 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for StatsManager."""

from __future__ import print_function
import json
import os
import shutil
import tempfile
import unittest

import stats_manager

class TestStatsManager(unittest.TestCase):
  """Test to verify StatsManager methods work as expected.

  StatsManager should collect raw data, calculate their statistics, and save
  them in expected format.
  """

  def _populate_dummy_stats(self):
    """Create a populated & processed StatsManager to test data retrieval."""
    self.data.AddValue('A', 99999.5)
    self.data.AddValue('A', 100000.5)
    self.data.AddValue('A', 'ERROR')
    self.data.SetUnit('A', 'uW')
    self.data.SetUnit('A', 'mW')
    self.data.AddValue('B', 1.5)
    self.data.AddValue('B', 2.5)
    self.data.AddValue('B', 3.5)
    self.data.SetUnit('B', 'mV')
    self.data.CalculateStats()

  def _populate_dummy_stats_no_unit(self):
    self.data.AddValue('B', 1000)
    self.data.AddValue('A', 200)
    self.data.SetUnit('A', 'blue')

  def setUp(self):
    """Set up StatsManager and create a temporary directory for test."""
    self.tempdir = tempfile.mkdtemp()
    self.data = stats_manager.StatsManager()

  def tearDown(self):
    """Delete the temporary directory and its content."""
    shutil.rmtree(self.tempdir)

  def test_AddValue(self):
    """Adding a value successfully adds a value."""
    self.data.AddValue('Test', 1000)
    self.data.SetUnit('Test', 'test')
    self.data.CalculateStats()
    summary = self.data.GetSummary()
    self.assertEqual(1, summary['Test']['count'])

  def test_AddValueNoFloat(self):
    """Adding a non number gets ignored and doesn't raise an exception."""
    self.data.AddValue('Test', 17)
    self.data.AddValue('Test', 'fiesta')
    self.data.SetUnit('Test', 'test')
    self.data.CalculateStats()
    summary = self.data.GetSummary()
    self.assertEqual(1, summary['Test']['count'])

  def test_AddValueNoUnit(self):
    """Not adding a unit does not cause an exception on CalculateStats()."""
    self.data.AddValue('Test', 17)
    self.data.CalculateStats()
    summary = self.data.GetSummary()
    self.assertEqual(1, summary['Test']['count'])

  def test_UnitSuffix(self):
    """Unit gets appended as a suffix in the displayed summary."""
    self.data.AddValue('test', 250)
    self.data.SetUnit('test', 'mw')
    self.data.CalculateStats()
    summary_str = self.data.SummaryToString()
    self.assertIn('test_mw', summary_str)

  def test_DoubleUnitSuffix(self):
    """If domain already ends in unit, verify that unit doesn't get appended."""
    self.data.AddValue('test_mw', 250)
    self.data.SetUnit('test_mw', 'mw')
    self.data.CalculateStats()
    summary_str = self.data.SummaryToString()
    self.assertIn('test_mw', summary_str)
    self.assertNotIn('test_mw_mw', summary_str)

  def test_GetRawData(self):
    """GetRawData returns exact same data as fed in."""
    self._populate_dummy_stats()
    raw_data = self.data.GetRawData()
    self.assertListEqual([99999.5, 100000.5], raw_data['A'])
    self.assertListEqual([1.5, 2.5, 3.5], raw_data['B'])

  def test_GetSummary(self):
    """GetSummary returns expected stats about the data fed in."""
    self._populate_dummy_stats()
    summary = self.data.GetSummary()
    self.assertEqual(2, summary['A']['count'])
    self.assertAlmostEqual(100000.5, summary['A']['max'])
    self.assertAlmostEqual(99999.5, summary['A']['min'])
    self.assertAlmostEqual(0.5, summary['A']['stddev'])
    self.assertAlmostEqual(100000.0, summary['A']['mean'])
    self.assertEqual(3, summary['B']['count'])
    self.assertAlmostEqual(3.5, summary['B']['max'])
    self.assertAlmostEqual(1.5, summary['B']['min'])
    self.assertAlmostEqual(0.81649658092773, summary['B']['stddev'])
    self.assertAlmostEqual(2.5, summary['B']['mean'])

  def test_SaveRawData(self):
    """SaveRawData stores same data as fed in."""
    self._populate_dummy_stats()
    dirname = 'unittest_raw_data'
    self.data.SaveRawData(self.tempdir, dirname)
    dirname = os.path.join(self.tempdir, dirname)
    file_a = os.path.join(dirname, 'A_mW.txt')
    file_b = os.path.join(dirname, 'B_mV.txt')
    with open(file_a, 'r') as f_a:
      self.assertEqual('99999.50', f_a.readline().strip())
      self.assertEqual('100000.50', f_a.readline().strip())
    with open(file_b, 'r') as f_b:
      self.assertEqual('1.50', f_b.readline().strip())
      self.assertEqual('2.50', f_b.readline().strip())
      self.assertEqual('3.50', f_b.readline().strip())

  def test_SaveRawDataNoUnit(self):
    """SaveRawData appends no unit suffix if the unit is not specified."""
    self._populate_dummy_stats_no_unit()
    self.data.CalculateStats()
    outdir = 'unittest_raw_data'
    self.data.SaveRawData(self.tempdir, outdir)
    files = os.listdir(os.path.join(self.tempdir, outdir))
    # Verify nothing gets appended to domain for filename if no unit exists.
    self.assertIn('B.txt', files)

  def test_SaveSummary(self):
    """SaveSummary properly dumps the summary into a file."""
    self._populate_dummy_stats()
    fname = 'unittest_summary.txt'
    self.data.SaveSummary(self.tempdir, fname)
    fname = os.path.join(self.tempdir, fname)
    with open(fname, 'r') as f:
      self.assertEqual(
          '@@   NAME  COUNT       MEAN  STDDEV        MAX       MIN\n',
          f.readline())
      self.assertEqual(
          '@@   A_mW      2  100000.00    0.50  100000.50  99999.50\n',
          f.readline())
      self.assertEqual(
          '@@   B_mV      3       2.50    0.82       3.50      1.50\n',
          f.readline())

  def test_SaveSummaryJSON(self):
    """SaveSummaryJSON saves the added data properly in JSON format."""
    self._populate_dummy_stats()
    fname = 'unittest_summary.json'
    self.data.SaveSummaryJSON(self.tempdir, fname)
    fname = os.path.join(self.tempdir, fname)
    with open(fname, 'r') as f:
      summary = json.load(f)
      self.assertAlmostEqual(100000.0, summary['A']['mean'])
      self.assertEqual('milliwatt', summary['A']['unit'])
      self.assertAlmostEqual(2.5, summary['B']['mean'])
      self.assertEqual('millivolt', summary['B']['unit'])

  def test_SaveSummaryJSONNoUnit(self):
    """SaveSummaryJSON marks unknown units properly as N/A."""
    self._populate_dummy_stats_no_unit()
    self.data.CalculateStats()
    fname = 'unittest_summary.json'
    self.data.SaveSummaryJSON(self.tempdir, fname)
    fname = os.path.join(self.tempdir, fname)
    with open(fname, 'r') as f:
      summary = json.load(f)
      self.assertEqual('blue', summary['A']['unit'])
      # if no unit is specified, JSON should save 'N/A' as the unit.
      self.assertEqual('N/A', summary['B']['unit'])

if __name__ == '__main__':
  unittest.main()
