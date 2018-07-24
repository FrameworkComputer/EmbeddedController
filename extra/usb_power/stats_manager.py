# Copyright 2017 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Calculates statistics for lists of data and pretty print them."""

from __future__ import print_function

import collections
import json
import logging
import os

import numpy

STATS_PREFIX = '@@'

LONG_UNIT = {
    '': 'N/A',
    'mW': 'milliwatt',
    'uW': 'microwatt',
    'mV': 'millivolt',
    'uA': 'microamp',
    'uV': 'microvolt'
}


class StatsManagerError(Exception):
  """Errors in StatsManager class."""
  pass


class StatsManager(object):
  """Calculates statistics for several lists of data(float).

  Example usage:

    >>> stats = StatsManager()
    >>> stats.AddSample(TIME_KEY, 50.0)
    >>> stats.AddSample(TIME_KEY, 25.0)
    >>> stats.AddSample(TIME_KEY, 40.0)
    >>> stats.AddSample(TIME_KEY, 10.0)
    >>> stats.AddSample(TIME_KEY, 10.0)
    >>> stats.AddSample('frobnicate', 11.5)
    >>> stats.AddSample('frobnicate', 9.0)
    >>> stats.AddSample('foobar', 11111.0)
    >>> stats.AddSample('foobar', 22222.0)
    >>> stats.CalculateStats()
    >>> print(stats.SummaryToString())
    @@            NAME  COUNT      MEAN   STDDEV       MAX       MIN
    @@   sample_msecs      4     31.25    15.16     50.00     10.00
    @@         foobar      2  16666.50  5555.50  22222.00  11111.00
    @@     frobnicate      2     10.25     1.25     11.50      9.00

  Attributes:
    _data: dict of list of readings for each domain(key)
    _unit: dict of unit for each domain(key)
    _order: list of formatting order for domains. Domains not listed are
            displayed in sorted order
    _hide_domains: collection of domains to hide when formatting summary string
    _summary: dict of stats per domain (key): min, max, count, mean, stddev
    _logger = StatsManager logger

  Note:
    _summary is empty until CalculateStats() is called, and is updated when
    CalculateStats() is called.
  """

  # pylint: disable=W0102
  def __init__(self, order=[], hide_domains=[]):
    """Initialize infrastructure for data and their statistics."""
    self._data = collections.defaultdict(list)
    self._unit = collections.defaultdict(str)
    self._order = order
    self._hide_domains = hide_domains
    self._summary = {}
    self._logger = logging.getLogger('StatsManager')

  def AddSample(self, domain, sample):
    """Add one sample for a domain.

    Args:
      domain: the domain name for the sample.
      sample: one time sample for domain, expect type float.
    """
    if isinstance(sample, int):
      sample = float(sample)
    if isinstance(sample, float):
      self._data[domain].append(sample)
      return
    self._logger.warn('sample %s for domain %s is not a number, thus ignored.',
                      sample, domain)

  def SetUnit(self, domain, unit):
    """Set the unit for a domain.

    There can be only one unit for each domain. Setting unit twice will
    overwrite the original unit.

    Args:
      domain: the domain name.
      unit: unit of the domain.
    """
    if domain in self._unit:
      self._logger.warn('overwriting the unit of %s, old unit is %s, new unit '
                        'is %s.', domain, self._unit[domain], unit)
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

  def SummaryToString(self, prefix=STATS_PREFIX):
    """Format summary into a string, ready for pretty print.

    See class description for format example.

    Args:
      prefix: start every row in summary string with prefix, for easier reading.

    Returns:
      formatted summary string.
    """
    headers = ('NAME', 'COUNT', 'MEAN', 'STDDEV', 'MAX', 'MIN')
    table = [headers]
    # determine what domains to display & and the order
    domains_to_display = set(self._summary.keys()) - set(self._hide_domains)
    display_order = [key for key in self._order if key in domains_to_display]
    domains_to_display -= set(display_order)
    display_order.extend(sorted(domains_to_display))
    for domain in display_order:
      stats = self._summary[domain]
      if not domain.endswith(self._unit[domain]):
        domain = '%s_%s' % (domain, self._unit[domain])
      row = [domain]
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

  def GetSummary(self):
    """Getter for summary."""
    return self._summary

  def SaveSummary(self, directory, fname='summary.txt', prefix=STATS_PREFIX):
    """Save summary to file.

    Args:
      directory: directory to save the summary in.
      fname: filename to save summary under.
      prefix: start every row in summary string with prefix, for easier reading.

    Returns:
      full path of summary save location
    """
    summary_str = self.SummaryToString(prefix=prefix) + '\n'

    if not os.path.exists(directory):
      os.makedirs(directory)
    fname = os.path.join(directory, fname)
    with open(fname, 'w') as f:
      f.write(summary_str)
    return fname

  def SaveSummaryJSON(self, directory, fname='summary.json'):
    """Save summary (only MEAN) into a JSON file.

    Args:
      directory: directory to save the JSON summary in.
      fname: filename to save summary under.

    Returns:
      full path of summary save location
    """
    data = {}
    for domain in self._summary:
      unit = LONG_UNIT.get(self._unit[domain], self._unit[domain])
      data_entry = {'mean': self._summary[domain]['mean'], 'unit': unit}
      data[domain] = data_entry
    if not os.path.exists(directory):
      os.makedirs(directory)
    fname = os.path.join(directory, fname)
    with open(fname, 'w') as f:
      json.dump(data, f)
    return fname

  def GetRawData(self):
    """Getter for all raw_data."""
    return self._data

  def SaveRawData(self, directory, dirname='raw_data'):
    """Save raw data to file.

    Args:
      directory: directory to create the raw data folder in.
      dirname: folder in which raw data live.

    Returns:
      list of full path of each domain's raw data save location
    """
    if not os.path.exists(directory):
      os.makedirs(directory)
    dirname = os.path.join(directory, dirname)
    if not os.path.exists(dirname):
      os.makedirs(dirname)
    fnames = []
    for domain, data in self._data.iteritems():
      if not domain.endswith(self._unit[domain]):
        domain = '%s_%s' % (domain, self._unit[domain])
      fname = '%s.txt' % domain
      fname = os.path.join(dirname, fname)
      with open(fname, 'w') as f:
        f.write('\n'.join('%.2f' % sample for sample in data) + '\n')
      fnames.append(fname)
    return fnames
