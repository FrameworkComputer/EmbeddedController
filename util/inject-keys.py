#!/usr/bin/env python
# -*- coding: utf-8 -*-
#
# Copyright 2016 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Note: This is a py2/3 compatible file.

from __future__ import print_function
import string
import subprocess
import sys


KEYMATRIX = {'`': (3, 1), '1': (6, 1), '2': (6, 4), '3': (6, 2), '4': (6, 3),
             '5': (3, 3), '6': (3, 6), '7': (6, 6), '8': (6, 5), '9': (6, 9),
             '0': (6, 8), '-': (3, 8), '=': (0, 8), 'q': (7, 1), 'w': (7, 4),
             'e': (7, 2), 'r': (7, 3), 't': (2, 3), 'y': (2, 6), 'u': (7, 6),
             'i': (7, 5), 'o': (7, 9), 'p': (7, 8), '[': (2, 8), ']': (2, 5),
             '\\': (3, 11), 'a': (4, 1), 's': (4, 4), 'd': (4, 2), 'f': (4, 3),
             'g': (1, 3), 'h': (1, 6), 'j': (4, 6), 'k': (4, 5), 'l': (4, 9),
             ';': (4, 8), '\'': (1, 8), 'z': (5, 1), 'x': (5, 4), 'c': (5, 2),
             'v': (5, 3), 'b': (0, 3), 'n': (0, 6), 'm': (5, 6), ',': (5, 5),
             '.': (5, 9), '/': (5, 8), ' ': (5, 11), '<right>': (6, 12),
             '<alt_r>': (0, 10), '<down>': (6, 11), '<tab>': (2, 1),
             '<f10>': (0, 4), '<shift_r>': (7, 7), '<ctrl_r>': (4, 0),
             '<esc>': (1, 1), '<backspace>': (1, 11), '<f2>': (3, 2),
             '<alt_l>': (6, 10), '<ctrl_l>': (2, 0), '<f1>': (0, 2),
             '<search>': (0, 1), '<f3>': (2, 2), '<f4>': (1, 2), '<f5>': (3, 4),
             '<f6>': (2, 4), '<f7>': (1, 4), '<f8>': (2, 9), '<f9>': (1, 9),
             '<up>': (7, 11), '<shift_l>': (5, 7), '<enter>': (4, 11),
             '<left>': (7, 12)}


UNSHIFT_TABLE = { '~': '`', '!': '1', '@': '2', '#': '3', '$': '4',
                  '%': '5', '^': '6', '&': '7', '*': '8', '(': '9',
                  ')': '0', '_': '-', '+': '=', '{': '[', '}': ']',
                  '|': '\\',
                  ':': ';', '"': "'", '<': ',', '>': '.', '?': '/'}

for c in string.ascii_lowercase:
    UNSHIFT_TABLE[c.upper()] = c


def inject_event(key, press):
    if len(key) >= 2 and key[0] != '<':
        key = '<' + key + '>'
    if key not in KEYMATRIX:
        print("%s: invalid key: %s" % (this_script, key))
        sys.exit(1)
    (row, col) = KEYMATRIX[key]
    subprocess.call(["ectool", "kbpress", str(row), str(col),
                     "1" if press else "0"])


def inject_key(key):
    inject_event(key, True)
    inject_event(key, False)


def inject_string(string):
    for c in string:
        if c in KEYMATRIX:
            inject_key(c)
        elif c in UNSHIFT_TABLE:
            inject_event("<shift_l>", True)
            inject_key(UNSHIFT_TABLE[c])
            inject_event("<shift_l>", False)
        else:
            print("unimplemented character:", c)
            sys.exit(1)


def usage():
    print("Usage: %s [-s <string>] [-k <key>]" % this_script,
          "[-p <pressed-key>] [-r <released-key>] ...")
    print("Examples:")
    print("%s -s MyPassw0rd -k enter" % this_script)
    print("%s -p ctrl_l -p alt_l -k f3 -r alt_l -r ctrl_l" % this_script)


def help():
    usage()
    print("Valid keys are:")
    i = 0
    for key in KEYMATRIX:
        print("%12s" % key, end='')
        i += 1
        if i % 4 == 0:
            print()
    print()
    print("angle brackets may be omitted")


def usage_check(asserted_condition, message):
    if asserted_condition:
        return
    print("%s:" % this_script, message)
    usage()
    sys.exit(1)


# -- main

this_script = sys.argv[0]
arg_len = len(sys.argv)

if arg_len > 1 and sys.argv[1] == "--help":
    help()
    sys.exit(0)

usage_check(arg_len > 1, "not enough arguments")
usage_check(arg_len % 2 == 1, "mismatched arguments")

for i in range(1, arg_len, 2):
    usage_check(sys.argv[i] in ("-s", "-k", "-p", "-r"),
                "unknown flag: %s" % sys.argv[i])

for i in range(1, arg_len, 2):
    flag = sys.argv[i]
    arg = sys.argv[i+1]
    if flag == "-s":
        inject_string(arg)
    elif flag == "-k":
        inject_key(arg)
    elif flag == "-p":
        inject_event(arg, True)
    elif flag == "-r":
        inject_event(arg, False)
