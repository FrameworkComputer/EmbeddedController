# Copyright 2017 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Calculates statistics for lists of data and pretty print them."""

from __future__ import print_function
import collections
import json
import numpy
import os

STATS_PREFIX = '@@'
KEY_PREFIX = '__'
# This prefix is used for keys that should not be shown in the summary tab, such
# as timeline keys.
NOSHOW_PREFIX = '!!'

LONG_UNIT = {
    'mW': 'milliwatt',
    'uW': 'microwatt',
    'mV': 'millivolt',
    'uA': 'microamp',
    'uV': 'microvolt'
}


class StatsManager(object):
  """Calculates statistics for several lists of data(float)."""

  def __init__(self):
    """Initialize infrastructure for data and their statistics."""
    self._data = collections.defaultdict(list)
    self._unit = {}
    self._summary = {}

  def AddValue(self, domain, value):
    """Add one value for a domain.

    Args:
      domain: the domain name for the value.
      value: one time reading for domain, expect type float.
    """
    if isinstance(value, int):
      value = float(value)
    if isinstance(value, float):
      self._data[domain].append(value)
      return
    print('Warning: value %s for domain %s is not a number, thus ignored.' %
          (value, domain))

  def SetUnit(self, domain, unit):
    """Set the unit for a domain.

    There can be only one unit for each domain. Setting unit twice will
    overwrite the original unit.

    Args:
      domain: the domain name.
      unit: unit of the domain.
    """
    if domain in self._unit:
      print('Warning: overwriting the unit of %s, old unit is %s, new unit is '
            '%s.' % (domain, self._unit[domain], unit))
    self._unit[domain] = unit

  def CalculateStats(self):
    """Calculate stats for all domain-data pairs.

    First erases all previous stats, then calculate stats for all data.
    """
    self._summary = {}
    for domain, data in self._data.iteritems():
      data_np = numpy.array(data)
      self._summary[domain] = {
          'mean': data_np.mean(),
          'min': data_np.min(),
          'max': data_np.max(),
          'stddev': data_np.std(),
          'count': data_np.size,
      }

  def _SummaryToString(self, prefix=STATS_PREFIX):
    """Format summary into a string, ready for pretty print.

    Args:
      prefix: start every row in summary string with prefix, for easier reading.
    """
    headers = ('NAME', 'COUNT', 'MEAN', 'STDDEV', 'MAX', 'MIN')
    table = [headers]
    for domain in sorted(self._summary.keys()):
      if domain.startswith(NOSHOW_PREFIX):
        continue
      stats = self._summary[domain]
      unit = self._unit[domain]
      domain_unit = domain.lstrip(KEY_PREFIX) + '_' + unit
      row = [domain_unit]
      row.append(str(stats['count']))
      for entry in headers[2:]:
        row.append('%.2f' % stats[entry.lower()])
      table.append(row)

    max_col_width = []
    for col_idx in range(len(table[0])):
      col_item_widths = [len(row[col_idx]) for row in table]
      max_col_width.append(max(col_item_widths))

    formatted_table = []
    for row in table:
      formatted_row = prefix + ' '
      for i in range(len(row)):
        formatted_row += row[i].rjust(max_col_width[i] + 2)
      formatted_table.append(formatted_row)
    return '\n'.join(formatted_table)

  def PrintSummary(self, prefix=STATS_PREFIX):
    """Print the formatted summary.

    Args:
      prefix: start every row in summary string with prefix, for easier reading.
    """
    summary_str = self._SummaryToString(prefix=prefix)
    print(summary_str)

  def GetSummary(self):
    """Getter for summary."""
    return self._summary

  def SaveSummary(self, directory, fname='summary.txt', prefix=STATS_PREFIX):
    """Save summary to file.

    Args:
      directory: directory to save the summary in.
      fname: filename to save summary under.
      prefix: start every row in summary string with prefix, for easier reading.
    """
    summary_str = self._SummaryToString(prefix=prefix) + '\n'

    if not os.path.exists(directory):
      os.makedirs(directory)
    fname = os.path.join(directory, fname)
    with open(fname, 'w') as f:
      f.write(summary_str)

  def SaveSummaryJSON(self, directory, fname='summary.json'):
    """Save summary (only MEAN) into a JSON file.

    Args:
      directory: directory to save the JSON summary in.
      fname: filename to save summary under.
    """
    data = {}
    for domain in self._summary:
      if domain.startswith(NOSHOW_PREFIX):
        continue
      unit = LONG_UNIT.get(self._unit[domain], self._unit[domain])
      data_entry = {'mean': self._summary[domain]['mean'], 'unit': unit}
      data[domain] = data_entry
    if not os.path.exists(directory):
      os.makedirs(directory)
    fname = os.path.join(directory, fname)
    with open(fname, 'w') as f:
      json.dump(data, f)

  def GetRawData(self):
    """Getter for all raw_data."""
    return self._data

  def SaveRawData(self, directory, dirname='raw_data'):
    """Save raw data to file.

    Args:
      directory: directory to create the raw data folder in.
      dirname: folder in which raw data live.
    """
    if not os.path.exists(directory):
      os.makedirs(directory)
    dirname = os.path.join(directory, dirname)
    if not os.path.exists(dirname):
      os.makedirs(dirname)
    for domain, data in self._data.iteritems():
      fname = domain + '_' + self._unit[domain] + '.txt'
      fname = os.path.join(dirname, fname)
      with open(fname, 'w') as f:
        f.write('\n'.join('%.2f' % value for value in data) + '\n')
