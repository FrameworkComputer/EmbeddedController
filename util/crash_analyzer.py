#!/usr/bin/env python3
# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""EC Crash report analyzer"""

import argparse
import pathlib
import re
import sys


# TODO(b/253492108): Add regexp for missing architectures.
# Regex tested here: https://regex101.com/r/K5S8cB/1
_REGEX_CORTEX_M0 = (
    r"^Saved.*$\n=== .* EXCEPTION: (.*) ====== xPSR: (.*) ===$\n"
    r"r0 :(.*) r1 :(.*) r2 :(.*) r3 :(.*)$\n"
    r"r4 :(.*) r5 :(.*) r6 :(.*) r7 :(.*)$\n"
    r"r8 :(.*) r9 :(.*) r10:(.*) r11:(.*)$\n"
    r"r12:(.*) sp :(.*) lr :(.*) pc :(.*)$\n"
    r"\n"
    r"^.*cfsr=(.*), shcsr=(.*), hfsr=(.*), dfsr=(.*), ipsr=(.*)$"
)

# Regex tested here: https://regex101.com/r/FL7T0n/1
_REGEX_NDS32 = (
    r"^Saved.*$\n===.*ITYPE=(.*) ===$\n"
    r"R0  (.*) R1  (.*) R2  (.*) R3  (.*)$\n"
    r"R4  (.*) R5  (.*) R6  (.*) R7  (.*)$\n"
    r"R8  (.*) R9  (.*) R10 (.*) R15 (.*)$\n"
    r"FP  (.*) GP  (.*) LP  (.*) SP  (.*)$\n"
    r"IPC (.*) IPSW (.*)$\n"
)


# List of symbols. Each entry is tuple: address, name
_symbols = []
# List of crashes. Each entry is dictionary.
# Contains all crashes found.
_entries = []


# This is function, and not a global dictionary, so that
# we can reference the different functions without placing
# the functions above the dictionary.
def get_architectures() -> list:
    """Returns a dictionary with the supported architectures"""

    archs = {
        "cm0": {
            "regex": _REGEX_CORTEX_M0,
            "parser": cm0_parse,
            "extra_regs": [
                "sp",
                "lr",
                "pc",
                "cfsr",
                "chcsr",
                "hfsr",
                "dfsr",
                "ipsr",
            ],
        },
        "nds32": {
            "regex": _REGEX_NDS32,
            "parser": nds32_parse,
            "extra_regs": ["fp", "gp", "lp", "sp", "ipc", "ipsw"],
        },
    }
    return archs


def get_crash_cause(cause: int) -> str:
    """Returns the cause of crash in human-readable format"""
    causes = {
        0xDEAD6660: "div-by-0",
        0xDEAD6661: "stack-overflow",
        0xDEAD6662: "pd-crash",
        0xDEAD6663: "assert",
        0xDEAD6664: "watchdog",
        0xDEAD6665: "bad-rng",
        0xDEAD6666: "pmic-fault",
        0xDEAD6667: "exit",
        0xDEAD6668: "watchdog-warning",
    }

    if cause in causes:
        return causes[cause]
    return f"unknown-cause-{format(cause, '#x')}"


def cm0_parse(match) -> dict:
    """Regex parser for Cortex-M0+ architecture"""

    # Expecting something like:
    # Saved panic data: (NEW)
    # === PROCESS EXCEPTION: ff ====== xPSR: ffffffff ===
    # r0 :         r1 :         r2 :         r3 :
    # r4 :dead6664 r5 :10092632 r6 :00000000 r7 :00000000
    # r8 :00000000 r9 :00000000 r10:00000000 r11:00000000
    # r12:         sp :00000000 lr :         pc :
    #
    # cfsr=00000000, shcsr=00000000, hfsr=00000000, dfsr=00000000, ipsr=000000ff
    regs = {}
    values = []

    for i in match.groups():
        try:
            val = int(i, 16)
        except ValueError:
            # Value might be empty, so we must handle the exception
            val = -1
        values.append(val)

    regs["task"] = values[0]
    regs["xPSR"] = values[1]
    regs["regs"] = values[3:15]
    regs["sp"] = values[15]
    regs["lr"] = values[16]
    regs["pc"] = values[17]
    regs["cfsr"] = values[18]
    regs["chcsr"] = values[19]
    regs["hfsr"] = values[20]
    regs["dfsr"] = values[21]
    regs["ipsr"] = values[22]

    regs["cause"] = get_crash_cause(values[6])  # r4

    # When CONFIG_DEBUG_STACK_OVERFLOW is enabled, the task number for a stack
    # overflow is saved in R5.
    if regs["cause"] == "stack-overflow":
        regs["task"] = values[7]  # r5

    # based on crash reports in case of asserrt the PC is in R3
    if regs["cause"] == "assert":
        regs["symbol"] = get_symbol(values[5])  # r3
        return regs

    # Heuristics: try link register, then PC, then what is believed to be PC.
    # When analyzing watchdogs, we try to be as close as possible to the caller
    # function that caused the watchdog.
    # That's why we prioritize LR (return address) over PC.
    if regs["lr"] != -1:
        regs["symbol"] = get_symbol(regs["lr"])
    elif regs["pc"] != -1:
        regs["symbol"] = get_symbol(regs["pc"])
    else:
        # Otherwise, if both LR and PC are empty, most probably
        # PC is in R5.
        regs["symbol"] = get_symbol(values[7])  # r5

    return regs


def nds32_parse(match) -> dict:
    """Regex parser for Andes (NDS32) architecture"""

    # Expecting something like:
    # Saved panic data: (NEW)
    # === EXCEP: ITYPE=0 ===
    # R0  00000000 R1  00000000 R2  00000000 R3  00000000
    # R4  00000000 R5  00000000 R6  dead6664 R7  00000000
    # R8  00000000 R9  00000000 R10 00000000 R15 00000000
    # FP  00000000 GP  00000000 LP  00000000 SP  00000000
    # IPC 00050d5e IPSW   00000
    # SWID of ITYPE: 0
    regs = {}
    values = []

    for i in match.groups():
        try:
            val = int(i, 16)
        except ValueError:
            # Value might be empty, so we must handle the exception
            val = -1
        values.append(val)

    # NDS32 is not reporting task info.
    regs["task"] = -1
    regs["regs"] = values[1:13]
    regs["fp"] = values[13]
    regs["gp"] = values[14]
    regs["lp"] = values[15]
    regs["sp"] = values[16]
    regs["ipc"] = values[17]
    regs["ipsw"] = values[18]

    regs["cause"] = get_crash_cause(values[7])  # r6
    regs["symbol"] = get_symbol(regs["ipc"])
    return regs


def read_map_file(map_file):
    """Reads the map file, and populates the _symbols list with the tuple address/name"""
    lines = map_file.readlines()
    for line in lines:
        addr_str, _, name = line.split(" ")
        addr = int(addr_str, 16)
        _symbols.append((addr, name.strip()))


def get_symbol_bisec(addr: int, low: int, high: int) -> str:
    """Finds the symbol using binary search"""
    # Element not found.
    if low > high:
        return f"invalid address: {format(addr, '#x')}"

    mid = (high + low) // 2

    # Corner case for last element.
    if mid == len(_symbols) - 1:
        if addr > _symbols[mid][0]:
            return f"invalid address: {format(addr, '#x')}"
        return _symbols[mid][1]

    if _symbols[mid][0] <= addr < _symbols[mid + 1][0]:
        symbol = _symbols[mid][1]
        # Start of a sequence of Thumb instructions. When this happens, return
        # the next address.
        if symbol == "$t":
            symbol = _symbols[mid + 1][1]
        return symbol

    if addr > _symbols[mid][0]:
        return get_symbol_bisec(addr, mid + 1, high)
    return get_symbol_bisec(addr, low, mid - 1)


def get_symbol(addr: int) -> str:
    """Returns the function name that corresponds to the given address"""
    symbol = get_symbol_bisec(addr, 0, len(_symbols) - 1)

    # Symbols generated by the compiler to identify transitions in the
    # code. If so, just append the address.
    if symbol in ("$a", "$d", "$c", "$t"):
        symbol = f"{symbol}:{format(addr,'#x')}"
    return symbol


def process_log_file(file_name: str) -> tuple:
    """Reads a .log file and extracts the EC and BIOS versions"""
    ec_ver = None
    bios_ver = None
    try:
        with open(file_name, "r", encoding="ascii") as log_file:
            lines = log_file.readlines()
            for line in lines:
                # Searching for something like:
                # ===ec_info===
                # vendor               | Nuvoton
                # name                 | NPCX586G
                # fw_version           | rammus_v2.0.460-d1d2aeb01f
                # ...
                # ===bios_info===
                # fwid       =   Google_Rammus.11275.193.0    #   [RO/str] ...
                # ro_fwid    =   Google_Rammus.11275.28.0     #   [RO/str] ...

                # Get EC version.
                # There could be more than one "fw_version". Only the first one
                # corresponds to the EC version.
                if line.startswith("RW version") and ec_ver is None:
                    _, ec_ver = line.split(":")
                    ec_ver = ec_ver.strip(" \n")
                if line.startswith("fw_version") and ec_ver is None:
                    _, ec_ver = line.split("|")
                    ec_ver = ec_ver.strip(" \n")

                # Get BIOS version.
                if line.startswith("fwid"):
                    _, value = line.split("=")
                    # Only get the first element.
                    bios_ver, _ = value.split("#")
                    bios_ver = bios_ver.strip()

                if ec_ver is not None and bios_ver is not None:
                    return ec_ver, bios_ver

    except FileNotFoundError:
        return ".log file not found", "not found"
    return (
        "unknown fw version" if ec_ver is None else ec_ver,
        "unknown BIOS version" if bios_ver is None else bios_ver,
    )


def process_crash_file(filename: str) -> dict:
    """Process a single crash report, and convert it to a dictionary"""

    with open(filename, "r", encoding="ascii") as crash_file:
        content = crash_file.read()

        for key, arch in get_architectures().items():
            regex = arch["regex"]
            match = re.match(regex, content, re.MULTILINE)
            if match is None:
                continue
            entry = arch["parser"](match)

            # Add "arch" entry since it is needed to process
            # the "extra_regs", among other things.
            entry["arch"] = key
            return entry
    return {}


def process_crash_files(crash_folder: pathlib.Path) -> None:
    """Process the crash reports that are in the crash_folder"""

    total = 0
    failed = 0
    good = 0
    for file in crash_folder.iterdir():
        # .log and .upload_file_eccrash might not be in order.
        # To avoid processing it more than once, only process the
        # ones with extension ".upload_file_eccrash" and then read the ".log".
        if file.suffix != ".upload_file_eccrash":
            continue
        entry = process_crash_file(file)
        if len(entry) != 0:
            ec_ver, bios_ver = process_log_file(
                file.parent.joinpath(file.stem + ".log")
            )
            entry["ec_version"] = ec_ver
            entry["bios_version"] = bios_ver
            entry["filename"] = file.stem

        if len(entry) != 0:
            _entries.append(entry)
            good += 1
        else:
            failed += 1
        total += 1
    print(f"Total: {total}, OK: {good}, Failed: {failed}", file=sys.stderr)


def cmd_report_lite(crash_folder: pathlib.Path, with_filename: bool) -> None:
    """Generates a 'lite' report that only contains a few fields"""

    process_crash_files(crash_folder)
    for entry in _entries:
        print(
            f"Task: {format(entry['task'],'#04x')} - "
            f"cause: {entry['cause']} - "
            f"PC: {entry['symbol']} - "
            f"{entry['ec_version']} - "
            f"{entry['bios_version']}",
            end="",
        )

        if with_filename:
            print(f" - {entry['filename']}", end="")

        print()


def cmd_report_full(crash_folder: pathlib.Path, with_filename: bool) -> None:
    """Generates a full report in .cvs format"""

    process_crash_files(crash_folder)
    # Print header
    for entry in _entries:
        print(
            f"Task: {format(entry['task'],'#04x')} - "
            f"cause: {entry['cause']} - "
            f"PC: {entry['symbol']} - ",
            end="",
        )
        # Print registers
        hex_regs = [hex(x) for x in entry["regs"]]
        print(f"{hex_regs} - ", end="")

        # Print extra registers. Varies from architecture to architecture.
        arch = get_architectures()[entry["arch"]]
        for reg in arch["extra_regs"]:
            print(f"{reg}: {format(entry[reg], '#x')} ", end="")

        # Print EC & BIOS info
        print(
            f"{entry['ec_version']} - {entry['bios_version']}",
            end="",
        )

        # Finally the filename. Useful for debugging.
        if with_filename:
            print(f" - {entry['filename']}", end="")

        print()


def main(argv):
    """Main entry point"""
    example_text = """Example:
# For further details see: go/cros-ec-crash-analyzer
#
# Summary:
# 1st:
# Collect the crash reports using this script:
# https://source.corp.google.com/piper///depot/google3/experimental/users/ricardoq/ec_crash_report_fetcher
# MUST be run within a Google3 Workspace. E.g:
(google3) blaze run //experimental/users/ricardoq/ec_crash_report_fetcher:main -- --outdir=/tmp/dumps/ --limit=3000 --offset=15000 --hwclass=shyvana --milestone=105

# 2nd:
# Assuming that you don't have the .map file of the EC image,  you can download the EC image from LUCI
# and then parse the .elf file by doing:
nm -n ec.RW.elf | grep " [tT] " > /tmp/rammus_193.map

# 3rd:
# Run this script
crash_analyzer.py full -m /tmp/rammus_193.map -f /tmp/dumps

# Combine it with 'sort' and 'uniq' for better reports. E.g:
crash_analyzer.py lite -m /tmp/rammus_193.map -f /tmp/dumps | sort | uniq -c | less

# Tip:
# Start by analyzing the "lite" report. If there is a function that calls your
# attention, generate the "full" report and analyze with Ghidra and/or
# IDA Pro the different "PC" that belong to the suspicious function.
"""

    parser = argparse.ArgumentParser(
        prog="crash_analyzer",
        epilog=example_text,
        description="Process crash reports and converts them to human-friendly format.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "-m",
        "--map-file",
        type=argparse.FileType("r"),
        required=True,
        metavar="ec_map_file",
        help="/path/to/ec_image_map_file",
    )
    parser.add_argument(
        "-f",
        "--crash-folder",
        type=pathlib.Path,
        required=True,
        help="Folder with the EC crash report files",
    )
    parser.add_argument(
        "-n",
        "--with-filename",
        action="store_true",
        help="Includes the filename in the report. Useful for debugging.",
    )
    parser.add_argument(
        "command", choices=["lite", "full"], help="Command to run."
    )
    args = parser.parse_args(argv)

    # Needed for all commands
    read_map_file(args.map_file)

    if args.command == "lite":
        cmd_report_lite(args.crash_folder, args.with_filename)
    elif args.command == "full":
        cmd_report_full(args.crash_folder, args.with_filename)
    else:
        print(f"Unsupported command: {args.command}")


if __name__ == "__main__":
    main(sys.argv[1:])
