# Copyright 2017 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Calculates statistics for lists of data and pretty print them."""

import collections
import json
import logging
import math
import os

import numpy  # pylint:disable=import-error


STATS_PREFIX = "@@"
NAN_TAG = "*"
NAN_DESCRIPTION = f"{NAN_TAG} domains contain NaN samples"

LONG_UNIT = {
    "": "N/A",
    "mW": "milliwatt",
    "uW": "microwatt",
    "mV": "millivolt",
    "uA": "microamp",
    "uV": "microvolt",
}


class StatsManagerError(Exception):
    """Errors in StatsManager class."""


class StatsManager:
    """Calculates statistics for several lists of data(float).

    Example usage:

      >>> stats = StatsManager(title='Title Banner')
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
    ` @@--------------------------------------------------------------
    ` @@                        Title Banner
      @@--------------------------------------------------------------
      @@            NAME  COUNT      MEAN   STDDEV       MAX       MIN
      @@   sample_msecs      4     31.25    15.16     50.00     10.00
      @@         foobar      2  16666.50  5555.50  22222.00  11111.00
      @@     frobnicate      2     10.25     1.25     11.50      9.00
    ` @@--------------------------------------------------------------

    Attributes:
      _data: dict of list of readings for each domain(key)
      _unit: dict of unit for each domain(key)
      _smid: id supplied to differentiate data output to other StatsManager
             instances that potentially save to the same directory
             if smid all output files will be named |smid|_|fname|
      _title: title to add as banner to formatted summary. If no title,
              no banner gets added
      _order: list of formatting order for domains. Domains not listed are
              displayed in sorted order
      _hide_domains: collection of domains to hide when formatting summary string
      _accept_nan: flag to indicate if NaN samples are acceptable
      _nan_domains: set to keep track of which domains contain NaN samples
      _summary: dict of stats per domain (key): min, max, count, mean, stddev
      _logger = StatsManager logger

    Note:
      _summary is empty until CalculateStats() is called, and is updated when
      CalculateStats() is called.
    """

    # pylint: disable=dangerous-default-value
    def __init__(
        self, smid="", title="", order=[], hide_domains=[], accept_nan=True
    ):
        """Initialize infrastructure for data and their statistics."""
        self._title = title
        self._data = collections.defaultdict(list)
        self._unit = collections.defaultdict(str)
        self._smid = smid
        self._order = order
        self._hide_domains = hide_domains
        self._accept_nan = accept_nan
        self._nan_domains = set()
        self._summary = {}
        self._logger = logging.getLogger(type(self).__name__)

    def AddSample(self, domain, sample):  # pylint: disable=invalid-name
        """Add one sample for a domain.

        Args:
          domain: the domain name for the sample.
          sample: one time sample for domain, expect type float.

        Raises:
          StatsManagerError: if trying to add NaN and |_accept_nan| is false
        """
        try:
            sample = float(sample)
        except ValueError:
            # if we don't accept nan this will be caught below
            self._logger.debug(
                "sample %s for domain %s is not a number. Making NaN",
                sample,
                domain,
            )
            sample = float("NaN")
        if not self._accept_nan and math.isnan(sample):
            raise StatsManagerError(
                "accept_nan is false. Cannot add NaN sample."
            )
        self._data[domain].append(sample)
        if math.isnan(sample):
            self._nan_domains.add(domain)

    def SetUnit(self, domain, unit):  # pylint: disable=invalid-name
        """Set the unit for a domain.

        There can be only one unit for each domain. Setting unit twice will
        overwrite the original unit.

        Args:
          domain: the domain name.
          unit: unit of the domain.
        """
        if domain in self._unit:
            self._logger.warning(
                "overwriting the unit of %s, old unit is %s, new "
                "unit is %s.",
                domain,
                self._unit[domain],
                unit,
            )
        self._unit[domain] = unit

    def CalculateStats(self):  # pylint: disable=invalid-name
        """Calculate stats for all domain-data pairs.

        First erases all previous stats, then calculate stats for all data.
        """
        self._summary = {}
        for domain, data in self._data.items():
            data_np = numpy.array(data)
            self._summary[domain] = {
                "mean": numpy.nanmean(data_np),
                "min": numpy.nanmin(data_np),
                "max": numpy.nanmax(data_np),
                "stddev": numpy.nanstd(data_np),
                "count": data_np.size,
            }

    @property
    def DomainsToDisplay(self):  # pylint: disable=invalid-name
        """List of domains that the manager will output in summaries."""
        return set(self._summary.keys()) - set(self._hide_domains)

    @property
    def NanInOutput(self):  # pylint: disable=invalid-name
        """Return whether any of the domains to display have NaN values."""
        return bool(len(set(self._nan_domains) & self.DomainsToDisplay))

    def _SummaryTable(self):  # pylint: disable=invalid-name
        """Generate the matrix to output as a summary.

        Returns:
          A 2d matrix of headers and their data for each domain
          e.g.
          [[NAME, COUNT, MEAN, STDDEV, MAX, MIN],
           [pp5000_mw, 10, 50, 0, 50, 50]]
        """
        headers = ("NAME", "COUNT", "MEAN", "STDDEV", "MAX", "MIN")
        table = [headers]
        # determine what domains to display & and the order
        domains_to_display = self.DomainsToDisplay
        display_order = [
            key for key in self._order if key in domains_to_display
        ]
        domains_to_display -= set(display_order)
        display_order.extend(sorted(domains_to_display))
        for domain in display_order:
            stats = self._summary[domain]
            if not domain.endswith(self._unit[domain]):
                domain = f"{domain}_{self._unit[domain]}"
            if domain in self._nan_domains:
                domain = f"{domain}{NAN_TAG}"
            row = [domain]
            row.append(str(stats["count"]))
            for entry in headers[2:]:
                row.append(f"{stats[entry.lower()]:.2f}")
            table.append(row)
        return table

    def SummaryToMarkdownString(self):  # pylint: disable=invalid-name
        """Format the summary into a b/ compatible markdown table string.

        This requires this sort of output format

        | header1   | header2   | header3   | ...
        | --------- | --------- | --------- | ...
        | sample1h1 | sample1h2 | sample1h3 | ...
        .
        .
        .

        Returns:
          formatted summary string.
        """
        # All we need to do before processing is insert a row of '-' between
        # the headers, and the data
        table = self._SummaryTable()
        columns = len(table[0])
        # Using '-:' to allow the numbers to be right aligned
        sep_row = ["-"] + ["-:"] * (columns - 1)
        table.insert(1, sep_row)
        text_rows = ["|".join(r) for r in table]
        body = "\n".join([f"|{r}|" for r in text_rows])
        if self._title:
            title_section = f"**{self._title}**  \n\n"
            body = title_section + body
        # Make sure that the body is terminated with a newline.
        return body + "\n"

    def SummaryToString(
        self, prefix=STATS_PREFIX
    ):  # pylint: disable=invalid-name
        """Format summary into a string, ready for pretty print.

        See class description for format example.

        Args:
          prefix: start every row in summary string with prefix, for easier reading.

        Returns:
          formatted summary string.
        """
        table = self._SummaryTable()
        max_col_width = []
        for col_idx in range(len(table[0])):
            col_item_widths = [len(row[col_idx]) for row in table]
            max_col_width.append(max(col_item_widths))

        formatted_lines = []
        for row in table:
            formatted_row = prefix + " "
            for row_text, col_width in zip(row, max_col_width):
                formatted_row += row_text.rjust(col_width + 2)
            formatted_lines.append(formatted_row)
        if self.NanInOutput:
            formatted_lines.append(f"{prefix} {NAN_DESCRIPTION}")

        if self._title:
            line_length = len(formatted_lines[0])
            dec_length = len(prefix)
            # trim title to be at most as long as the longest line without the prefix
            title = self._title[: (line_length - dec_length)]
            # line is a separator line consisting of -----
            line = f"{prefix}{'-' * (line_length - dec_length)}"
            # prepend the prefix to the centered title
            padded_title = f"{prefix}{title.center(line_length)[dec_length:]}"
            formatted_lines = (
                [line, padded_title, line] + formatted_lines + [line]
            )
        formatted_output = "\n".join(formatted_lines)
        return formatted_output

    def GetSummary(self):  # pylint: disable=invalid-name
        """Getter for summary."""
        return self._summary

    def _MakeUniqueFName(self, fname):  # pylint: disable=invalid-name
        """prepend |_smid| to fname & rotate fname to ensure uniqueness.

        Before saving a file through the StatsManager, make sure that the filename
        is unique, first by prepending the smid if any and otherwise by appending
        increasing integer suffixes until the filename is unique.

        If |smid| is defined /path/to/example/file.txt becomes
        /path/to/example/{smid}_file.txt.

        The rotation works by changing /path/to/example/somename.txt to
        /path/to/example/somename1.txt if the first one already exists on the
        system.

        Note: this is not thread-safe. While it makes sense to use StatsManager
        in a threaded data-collection, the data retrieval should happen in a
        single threaded environment to ensure files don't get potentially clobbered.

        Args:
          fname: filename to ensure uniqueness.

        Returns:
          {smid_}fname{tag}.[b].ext
          the smid portion gets prepended if |smid| is defined
          the tag portion gets appended if necessary to ensure unique fname
        """
        fdir = os.path.dirname(fname)
        base, ext = os.path.splitext(os.path.basename(fname))
        if self._smid:
            base = f"{self._smid}_{base}"
        unique_fname = os.path.join(fdir, f"{base}{ext}")
        tag = 0
        while os.path.exists(unique_fname):
            old_fname = unique_fname
            unique_fname = os.path.join(fdir, f"{base}{tag:d}{ext}")
            self._logger.warning(
                "Attempted to store stats information at %s, but "
                "file already exists. Attempting to store at %s "
                "now.",
                old_fname,
                unique_fname,
            )
            tag += 1
        return unique_fname

    def SaveSummary(
        self, directory, fname="summary.txt", prefix=STATS_PREFIX
    ):  # pylint: disable=invalid-name
        """Save summary to file.

        Args:
          directory: directory to save the summary in.
          fname: filename to save summary under.
          prefix: start every row in summary string with prefix, for easier reading.

        Returns:
          full path of summary save location
        """
        summary_str = self.SummaryToString(prefix=prefix) + "\n"
        return self._SaveSummary(summary_str, directory, fname)

    def SaveSummaryJSON(
        self, directory, fname="summary.json"
    ):  # pylint: disable=invalid-name
        """Save summary (only MEAN) into a JSON file.

        Args:
          directory: directory to save the JSON summary in.
          fname: filename to save summary under.

        Returns:
          full path of summary save location
        """
        data = {}
        for domain, item in self._summary.items():
            unit = LONG_UNIT.get(self._unit[domain], self._unit[domain])
            data_entry = {"mean": item["mean"], "unit": unit}
            data[domain] = data_entry
        summary_str = json.dumps(data, indent=2)
        return self._SaveSummary(summary_str, directory, fname)

    def SaveSummaryMD(
        self, directory, fname="summary.md"
    ):  # pylint: disable=invalid-name
        """Save summary into a MD file to paste into b/.

        Args:
          directory: directory to save the MD summary in.
          fname: filename to save summary under.

        Returns:
          full path of summary save location
        """
        summary_str = self.SummaryToMarkdownString()
        return self._SaveSummary(summary_str, directory, fname)

    def _SaveSummary(
        self, output_str, directory, fname
    ):  # pylint: disable=invalid-name
        """Wrote |output_str| to |fname|.

        Args:
          output_str: formatted output string
          directory: directory to save the summary in.
          fname: filename to save summary under.

        Returns:
          full path of summary save location
        """
        if not os.path.exists(directory):
            os.makedirs(directory)
        fname = self._MakeUniqueFName(os.path.join(directory, fname))
        with open(fname, "w", encoding="utf-8") as fff:
            fff.write(output_str)
        return fname

    def GetRawData(self):  # pylint: disable=invalid-name
        """Getter for all raw_data."""
        return self._data

    def SaveRawData(
        self, directory, dirname="raw_data"
    ):  # pylint: disable=invalid-name
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
        for domain, data in self._data.items():
            if not domain.endswith(self._unit[domain]):
                domain = f"{domain}_{self._unit[domain]}"
            fname = self._MakeUniqueFName(
                os.path.join(dirname, f"{domain}.txt")
            )
            with open(fname, "w", encoding="utf-8") as output_file:
                output_file.write(
                    "\n".join(f"{sample:.2f}" for sample in data) + "\n"
                )
            fnames.append(fname)
        return fnames
