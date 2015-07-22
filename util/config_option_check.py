#!/usr/bin/python2
# Copyright 2015 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Configuration Option Checker.

Script to ensure that all configuration options for the Chrome EC are defined
in config.h.
"""
from __future__ import print_function
import os
import re
import subprocess


class Line(object):
  """Class for each changed line in diff output.

  Attributes:
    line_num: The integer line number that this line appears in the file.
    string: The literal string of this line.
  """

  def __init__(self, line_num, string):
    """Inits Line with the line number and the actual string."""
    self.line_num = line_num
    self.string = string


class Hunk(object):
  """Class for a git diff hunk.

  Attributes:
    filename: The name of the file that this hunk belongs to.
    lines: A list of Line objects that are a part of this hunk.
  """

  def __init__(self, filename, lines):
    """Inits Hunk with the filename and the list of lines of the hunk."""
    self.filename = filename
    self.lines = lines


# Master file which is supposed to include all CONFIG_xxxx descriptions.
CONFIG_FILE = 'include/config.h'

# Specific files which the checker should ignore.
WHITELIST = [CONFIG_FILE, 'util/config_option_check.py']

def obtain_current_config_options():
  """Obtains current config options from include/config.h.

  Scans through the master config file defined in CONFIG_FILE for all CONFIG_*
  options.

  Returns:
    A list of all the config options in the master CONFIG_FILE.
  """

  config_options = []
  config_option_re = re.compile(r'^#(define|undef)\s+(CONFIG_[A-Z0-9_]+)')
  with open(CONFIG_FILE, 'r') as config_file:
    for line in config_file:
      result = config_option_re.search(line)
      if not result:
        continue
      word = result.groups()[1]
      if word not in config_options:
        config_options.append(word)
  return config_options

def print_missing_config_options(hunks, config_options):
  """Searches thru all the changes in hunks for missing options and prints them.

  TODO(aaboagye): Improve upon this to detect when files are being removed and a
  CONFIG_* option is no longer used elsewhere in the repo.

  Args:
    hunks: A list of Hunk objects which represent the hunks from the git
      diff output.
    config_options: A list of all the config options in the master CONFIG_FILE.

  Returns:
    missing_config_option: A boolean indicating if any CONFIG_* options
      are missing from the master CONFIG_FILE in this commit.
  """
  missing_config_option = False
  print_banner = True
  # Determine longest CONFIG_* length to be used for formatting.
  max_option_length = max(len(option) for option in config_options)
  config_option_re = re.compile(r'\b(CONFIG_[a-zA-Z0-9_]+)')
  c_style_ext = ('.c', '.h', '.inc', '.S')
  make_style_ext = ('.mk')

  # Check each hunk's line for a missing config option.
  for h in hunks:
    for l in h.lines:
      # Check for the existence of a CONFIG_* in the line.
      match = config_option_re.findall(l.string)
      if not match:
        continue

      # At this point, an option was found in the string.  However, we need to
      # verify that it is not within a comment.  Assume every option is a
      # violation until proven otherwise.

      violations = set()
      for option in match:
        violations.add(option)

      # Different files have different comment syntax;  Handle appropriately.
      extension = os.path.splitext(h.filename)[1]
      if extension in c_style_ext:
        beg_comment_idx = l.string.find('/*')
        end_comment_idx = l.string.find('*/')
        if end_comment_idx == -1:
          end_comment_idx = len(l.string)
        for option in match:
          option_idx = l.string.find(option)
          if beg_comment_idx == -1:
            # Check to see if this line is from a multi-line comment.
            if l.string.lstrip()[0] == '*':
              # It _seems_ like it is, therefore ignore this instance.
              violations.remove(option)
          else:
            # Check to see if its actually inside the comment.
            if beg_comment_idx < option_idx < end_comment_idx:
              # The config option is in the comment.  Ignore it.
              violations.remove(option)
      elif extension in make_style_ext or 'Makefile' in h.filename:
        beg_comment_idx = l.string.find('#')
        for option in match:
          option_idx = l.string.find(option)
          # Ignore everything to the right of the hash.
          if beg_comment_idx < option_idx and beg_comment_idx != -1:
            # The option is within a comment.  Ignore it.
            violations.remove(option)

      # Check to see if the CONFIG_* option is in the config file and print the
      # violations.
      for option in match:
        if option not in config_options and option in violations:
          # Print the banner once.
          if print_banner:
            print('The following config options were found to be missing '
                  'from %s.\n'
                  'Please add new config options there along with '
                  'descriptions.\n\n' % CONFIG_FILE)
            print_banner = False
            missing_config_option = True
          # Print the misssing config option.
          print('> %-*s %s:%s' % (max_option_length, option,
                                  h.filename,
                                  l.line_num))
  return missing_config_option

def get_hunks():
  """Gets the hunks of the most recent commit.

  States:
    new_file: Searching for a new file in the git diff.
    filename_search: Searching for the filename of this hunk.
    hunk: Searching for the beginning of a new hunk.
    lines: Counting line numbers and searching for changes.

  Returns:
    hunks: A list of Hunk objects which represent the hunks in the git diff
      output.
  """

  # Get the diff output.
  diff = []
  hunks = []
  hunk_lines = []
  line = ''
  filename = ''
  i = 0
  line_num = 0

  # Regex patterns
  new_file_re = re.compile(r'^diff --git')
  filename_re = re.compile(r'^[+]{3} (.*)')
  hunk_line_num_re = re.compile(r'^@@ -[0-9]+,[0-9]+ \+([0-9]+),[0-9]+ @@.*')
  line_re = re.compile(r'^([+| ])(.*)')

  cmd = 'git diff --cached -GCONFIG_* --no-prefix --no-ext-diff HEAD~1'
  diff = subprocess.check_output(cmd.split()).split('\n')
  line = diff[0]
  current_state = 'new_file'

  while True:
    # Search for the beginning of a new file.
    if current_state is 'new_file':
      match = new_file_re.search(line)
      if match:
        current_state = 'filename_search'

    # Search the diff output for a file name.
    elif current_state is 'filename_search':
      # If we're deleting a file, ignore it entirely.
      if 'deleted' in line:
        current_state = 'new_file'
        continue

      # Search for a file name.
      match = filename_re.search(line)
      if match:
        filename = match.groups(1)[0]
        if filename in WHITELIST:
          # Skip the file if it's whitelisted.
          current_state = 'new_file'
        else:
          current_state = 'hunk'

    # Search for a hunk.  Each hunk starts with a line describing the line
    # numbers in the file.
    elif current_state is 'hunk':
      hunk_lines = []
      match = hunk_line_num_re.search(line)
      if match:
        # Extract the line number offset.
        line_num = int(match.groups(1)[0])
        current_state = 'lines'

    # Start looking for changes.
    elif current_state is 'lines':
      # Check if state needs updating.
      new_hunk = hunk_line_num_re.search(line)
      new_file = new_file_re.search(line)
      if new_hunk:
        current_state = 'hunk'
        hunks.append(Hunk(filename, hunk_lines))
        continue
      elif new_file:
        current_state = 'new_file'
        hunks.append(Hunk(filename, hunk_lines))
        continue

      match = line_re.search(line)
      if match:
        # Consider this line iff it's an addition.
        if match.groups(1)[0] == '+':
          hunk_lines.append(Line(line_num, match.groups(2)[1]))
        line_num += 1

    # Advance to the next line
    try:
      i += 1
      line = diff[i]
    except IndexError:
      # We've reached the end of the diff.  Return what we have.
      if hunk_lines:
        hunks.append(Hunk(filename, hunk_lines))
      return hunks

def main():
  """Searches through committed changes for missing config options.

  Checks through committed changes for CONFIG_* options.  Then checks to make
  sure that all CONFIG_* options used are defined in include/config.h.  Finally,
  reports any missing config options.
  """
  # Obtain the hunks of the commit to search through.
  hunks = get_hunks()
  # Obtain config options from include/config.h.
  config_options = obtain_current_config_options()
  # Find any missing config options from the hunks and print them.
  missing_opts = print_missing_config_options(hunks, config_options)

  if missing_opts:
    print('\nIt may also be possible that you have a typo.')
    os.sys.exit(1)

if __name__ == '__main__':
  main()
