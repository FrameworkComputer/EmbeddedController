# Copyright 2022 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Code for auto-generating README.md."""

import argparse
import io


class MarkdownHelpFormatter(argparse.HelpFormatter):
    """Callbacks to format help output as Markdown."""

    def __init__(self, prog):
        self._prog = prog
        self._section_title = None
        self._section_contents = []
        self._paragraphs = []
        super().__init__(prog=prog)

    def add_text(self, text):
        if text and text is not argparse.SUPPRESS:
            lst = self._paragraphs
            if self._section_title:
                lst = self._section_contents
            lst.append(text)

    def start_section(self, title):
        self._section_title = title.title()
        self._section_contents = []

    def end_section(self):
        if self._section_contents:
            self._paragraphs.append(f"#### {self._section_title}")
            self._paragraphs.extend(self._section_contents)
        self._section_title = None

    def add_usage(self, usage, actions, groups):
        if not usage:
            usage = self._prog
        self.add_text(
            f"**Usage:** `{usage} {self._format_actions_usage(actions, groups)}`"
        )

    def add_arguments(self, actions):
        def _get_metavar(action):
            return action.metavar or action.dest

        def _format_invocation(action):
            if action.option_strings:
                parts = []
                for option_string in action.option_strings:
                    if action.nargs == 0:
                        parts.append(option_string)
                    else:
                        parts.append(f"{option_string} {_get_metavar(action).upper()}")
                return ", ".join(f"`{part}`" for part in parts)
            else:
                return f"`{_get_metavar(action)}`"

        def _get_table_line(action):
            return f"| {_format_invocation(action)} | {action.help} |"

        table_lines = [
            "|   |   |",
            "|---|---|",
            *(
                _get_table_line(action)
                for action in actions
                if action.help is not argparse.SUPPRESS
            ),
        ]

        # Don't want a table with no rows.
        if len(table_lines) > 2:
            self.add_text("\n".join(table_lines))

    def format_help(self):
        return "\n\n".join(self._paragraphs)


def generate_readme():
    """Generate the README.md file.

    Returns:
        A string with the README contents.
    """
    # Deferred import position to avoid circular dependency.
    # Normally, this would not be required, since we don't use from
    # imports.  But runpy's import machinery essentially does the
    # equivalent of a from import on __main__.py.
    import zmake.__main__

    output = io.StringIO()
    parser, sub_action = zmake.__main__.get_argparser()

    def _append(*args, **kwargs):
        kwargs.setdefault("file", output)
        print(*args, **kwargs)

    def _append_argparse_help(parser):
        parser.formatter_class = MarkdownHelpFormatter
        _append(parser.format_help())

    _append("# Zmake")
    _append()
    _append('<!-- Auto-generated contents!  Run "zmake generate-readme" to update. -->')
    _append()
    _append("[TOC]")
    _append()
    _append("## Usage")
    _append()
    _append_argparse_help(parser)
    _append()
    _append("## Subcommands")

    for sub_name, sub_parser in sub_action.choices.items():
        _append()
        _append(f"### zmake {sub_name}")
        _append()
        _append_argparse_help(sub_parser)

    return output.getvalue()
