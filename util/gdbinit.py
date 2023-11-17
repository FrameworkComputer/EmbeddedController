#!/usr/bin/env python3
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""This source parses env variables and sets up the GDB EC debug environment.

Usage from .gdbinit:
    source util/gdbinit.py

Environment Variables:
    BOARD=[nocturne_fp|bloonchipper|...]
    GDBSERVER=[segger|openocd]
    GDBPORT=[gdb-server-port-number]
    USING_CLION=[FALSE|TRUE]
"""

import distutils.util
import os
import pathlib
import textwrap

import gdb


BOARD = os.getenv("BOARD", "")
# PROJECT can be changed to be the name of a unit test, such as "sha256"
PROJECT = os.getenv("PROJECT", "ec")
GDBSERVER = os.getenv("GDBSERVER", "openocd")
USING_CLION = distutils.util.strtobool(os.getenv("USING_CLION", "FALSE"))

if GDBSERVER == "openocd":
    DEFAULT_GDB_PORT = "3333"
else:
    DEFAULT_GDB_PORT = "2331"

GDBPORT = os.getenv("GDBPORT", DEFAULT_GDB_PORT)

EC_DIR = pathlib.Path(
    os.getenv(
        "EC_DIR", os.path.join(os.getenv("HOME"), "chromiumos/src/platform/ec")
    )
)

if not EC_DIR.is_dir():
    print(f"Error - EC_DIR {EC_DIR} doesn't exist. Aborting.")
    gdb.execute("quit")

gdb.execute(f'set $BOARD = "{BOARD}"')
gdb.execute(f'set $GDBSERVER = "{GDBSERVER}"')
gdb.execute(f'set $GDBPORT = "{GDBPORT}"')

POST_REMOTE_HOOK = ""

if BOARD != "":
    print("# Setting up symbol files.")
    build_dir = EC_DIR / "build" / BOARD
    if PROJECT != "ec":
        build_dir = build_dir / PROJECT

    obj_elf = build_dir / (PROJECT + ".obj")
    ro_elf = build_dir / "RO" / (PROJECT + ".RO.elf")
    rw_elf = build_dir / "RW" / (PROJECT + ".RW.elf")

    for path in [obj_elf, ro_elf, rw_elf]:
        if not path.exists():
            print(f"Error - {path} doesn't exist. Aborting.")
            gdb.execute("quit")

    # Monitor commands must be run after connecting to the remote target
    # See https://stackoverflow.com/a/39828850
    # https://youtrack.jetbrains.com/issue/CPP-7322
    # https://sourceware.org/gdb/onlinedocs/gdb/Hooks.html
    POST_REMOTE_HOOK = textwrap.dedent(
        f"""
        define target hookpost-remote
            echo Flashing EC binary\\n
            load "{obj_elf}"
            echo Resetting target\\n
            monitor reset
            echo Setting breakpoint on main\\n
            b main
        end\n
        """
    )

    # When using gdb from CLion, this kills gdb.
    if not USING_CLION:
        gdb.execute(f"file {obj_elf}")
    gdb.execute(f"add-symbol-file {ro_elf}")
    gdb.execute(f"add-symbol-file {rw_elf}")

if GDBSERVER == "openocd":
    gdb.execute("set $GDBSERVER_OPENOCD = 1")
    gdb.execute("set $GDBSERVER_SEGGER = 0")
elif GDBSERVER == "segger":
    gdb.execute("set $GDBSERVER_OPENOCD = 0")
    gdb.execute("set $GDBSERVER_SEGGER = 1")
    if BOARD != "":
        gdb.execute(POST_REMOTE_HOOK)
else:
    print('Error - GDBSERVER="' + GDBSERVER + '" is invalid.')
    gdb.execute("quit")
