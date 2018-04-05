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

from stats_manager import StatsManager


class TestStatsManager(unittest.TestCase):
  """Test to verify StatsManager methods work as expected.

  StatsManager should collect raw data, calculate their statistics, and save
  them in expected format.
  """

  def setUp(self):
    """Set up data and create a temporary directory to save data and stats."""
    self.tempdir = tempfile.mkdtemp()
    self.data = StatsManager()
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

  def tearDown(self):
    """Delete the temporary directory and its content."""
    shutil.rmtree(self.tempdir)

  def test_GetRawData(self):
    raw_data = self.data.GetRawData()
    self.assertListEqual([99999.5, 100000.5], raw_data['A'])
    self.assertListEqual([1.5, 2.5, 3.5], raw_data['B'])

  def test_GetSummary(self):
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
    dirname = 'unittest_raw_data'
    self.data.SaveRawData(self.tempdir, dirname)
    dirname = os.path.join(self.tempdir, dirname)
    fileA = os.path.join(dirname, 'A_mW.txt')
    fileB = os.path.join(dirname, 'B_mV.txt')
    with open(fileA, 'r') as fA:
      self.assertEqual('99999.50', fA.readline().strip())
      self.assertEqual('100000.50', fA.readline().strip())
    with open(fileB, 'r') as fB:
      self.assertEqual('1.50', fB.readline().strip())
      self.assertEqual('2.50', fB.readline().strip())
      self.assertEqual('3.50', fB.readline().strip())

  def test_SaveSummary(self):
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
    fname = 'unittest_summary.json'
    self.data.SaveSummaryJSON(self.tempdir, fname)
    fname = os.path.join(self.tempdir, fname)
    with open(fname, 'r') as f:
      summary = json.load(f)
      self.assertAlmostEqual(100000.0, summary['A']['mean'])
      self.assertEqual('milliwatt', summary['A']['unit'])
      self.assertAlmostEqual(2.5, summary['B']['mean'])
      self.assertEqual('millivolt', summary['B']['unit'])


if __name__ == '__main__':
  unittest.main()
