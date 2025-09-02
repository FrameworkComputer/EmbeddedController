#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Find unused sources, functions and configs by comparing sources to build artifacts.

This script relies on heuristics and DWARF info extraction using pyelftools.
Results must be interpreted by a developer.

Limitations:
- Cannot reliably detect unused header files (.h) without conservative heuristics.
- May miss files included via assembly .inc directives if not in symbol table.
- Heuristics for function definition detection may have false positives/negatives.
- Requires ELF files with debug symbols to be present in build/ directory.
"""

import argparse
from concurrent.futures import ProcessPoolExecutor
import os
from pathlib import Path
import re
import subprocess
import sys

from elftools.elf.elffile import ELFFile  # pylint: disable=import-error


MAX_WORKERS = os.cpu_count() or 4
print(f"Using {MAX_WORKERS} workers")


def find_files(paths, patterns):
    """Find files using native find for efficiency."""
    sources = set()
    filter_pattern = []
    for pattern in patterns:
        if filter_pattern:
            filter_pattern.append("-o")
        filter_pattern.extend(["-name", pattern])
    for path in paths:
        if not path.exists():
            continue
        cmd = ["find", str(path), "-type", "f"] + filter_pattern
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            check=False,
        )
        found_files = [Path(p) for p in result.stdout.splitlines()]
        sources.update(found_files)
    return sources


def sources_from_elf(elf_path) -> list:
    """Find sources used in elf using pyelftools."""
    sources = set()
    try:
        with open(elf_path, "rb") as f:
            elffile = ELFFile(f)
            if not elffile.has_dwarf_info():
                return []

            dwarfinfo = elffile.get_dwarf_info()
            for cu in dwarfinfo.iter_CUs():
                line_program = dwarfinfo.line_program_for_CU(cu)
                if not line_program:
                    continue

                for file_entry in line_program["file_entry"]:
                    name = file_entry.name.decode("utf-8")
                    dir_index = file_entry.dir_index

                    if dir_index > 0:
                        # dir_index is 1-based, include_directory is 0-based
                        dir_path = line_program["include_directory"][
                            dir_index - 1
                        ].decode("utf-8")
                        full_path = Path(dir_path) / name
                    else:
                        # dir_index 0 typically implies the comp_dir of the CU
                        top_die = cu.get_top_DIE()
                        comp_dir_attr = top_die.attributes.get("DW_AT_comp_dir")
                        if comp_dir_attr and not Path(name).is_absolute():
                            comp_dir = comp_dir_attr.value.decode("utf-8")
                            full_path = Path(comp_dir) / name
                        else:
                            full_path = Path(name)

                    # Filter out artificial sources like <built-in>
                    if not (
                        full_path.name.startswith("<")
                        and full_path.name.endswith(">")
                    ):
                        sources.add(full_path)
    except Exception:  # pylint: disable=broad-exception-caught
        # Silently fail if ELF parsing fails
        pass

    return list(sources)


def find_functions_in_elf(elf_path) -> list:
    """Find functions used in elf using pyelftools."""
    functions = []
    try:
        with open(elf_path, "rb") as f:
            elffile = ELFFile(f)
            if not elffile.has_dwarf_info():
                return []

            dwarfinfo = elffile.get_dwarf_info()
            for cu in dwarfinfo.iter_CUs():
                top_die = cu.get_top_DIE()
                comp_dir = ""
                cu_name = ""

                if top_die.tag == "DW_TAG_compile_unit":
                    name_attr = top_die.attributes.get("DW_AT_name")
                    comp_dir_attr = top_die.attributes.get("DW_AT_comp_dir")
                    if name_attr:
                        cu_name = name_attr.value.decode("utf-8")
                    if comp_dir_attr:
                        comp_dir = comp_dir_attr.value.decode("utf-8")

                for die in cu.iter_DIEs():
                    if die.tag == "DW_TAG_subprogram":
                        name_attr = die.attributes.get("DW_AT_name")
                        if name_attr:
                            func_name = name_attr.value.decode("utf-8")

                            if cu_name:
                                if not Path(cu_name).is_absolute() and comp_dir:
                                    full_path = Path(comp_dir) / cu_name
                                else:
                                    full_path = Path(cu_name)
                                functions.append((full_path, func_name))
    except Exception:  # pylint: disable=broad-exception-caught
        pass

    return functions


def find_configs(paths) -> set:
    """Find all configs referenced in the source code."""
    configs = set()
    config_pattern = "CONFIG_[A-Z0-9_]*[A-Z0-9]"
    for path in paths:
        cmd = [
            "grep",
            "--recursive",
            "--no-filename",
            "--binary-files=without-match",
            "--only-matching",
            "--extended-regexp",
            config_pattern,
            str(path),
        ]
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            check=False,
        )
        configs.update(result.stdout.splitlines())
    return configs


def find_set_configs(paths) -> set:
    """Find all enabled configs in .config build artifacts."""
    configs = set()
    config_set_pattern = "^CONFIG_[A-Z0-9_]*[A-Z0-9]+=.*"
    for path in paths:
        cmd = [
            "grep",
            "--recursive",
            "--no-filename",
            "--binary-files=without-match",
            "--extended-regexp",
            "--include=ec.config",
            "--include=.config",
            config_set_pattern,
            str(path),
        ]
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            check=False,
        )
        configs.update({c.split("=")[0] for c in result.stdout.splitlines()})
    return configs


def find_functions_in_file(path):
    """Find all defined functions in a file."""
    functions = []
    try:
        with open(path, "r", errors="ignore", encoding="utf-8") as f:
            content = f.read()
    except Exception:  # pylint: disable=broad-exception-caught
        return []
    matches = re.findall(
        r"^(\w+[\w:*\s]+\s\**([\w.:]+)\([\w\s,*]*\))\s*{", content, re.MULTILINE
    )
    for match in matches:
        functions.append((path, match[1]))
    return functions


def find_all_defined_functions(paths) -> set:
    """Find all defined functions in all source paths."""
    functions = set()
    sources = find_files(
        paths=paths, patterns=["*.c", "*.h", "*.cc", "*.S", "*.inc"]
    )
    for source in sources:
        functions.update(find_functions_in_file(source))
    return functions


def normalize_path(ec_root, zephyr_root, path):
    """Normalize path to be relative to ec_root and handle cros_sdk mounts."""
    path = Path(path)

    try:
        path = path.resolve()
    except Exception:  # pylint: disable=broad-exception-caught
        pass

    path_str = str(path)

    if "/mnt/host/source/src/platform/ec/" in path_str:
        path = Path(path_str.split("/mnt/host/source/src/platform/ec/")[1])
    elif "/mnt/host/source/src/platform/ec-legacy/" in path_str:
        path = Path(
            path_str.split("/mnt/host/source/src/platform/ec-legacy/")[1]
        )
    elif "/mnt/host/source/src/third_party/zephyr/main/" in path_str:
        path = (
            Path("zephyr-main")
            / path_str.split("/mnt/host/source/src/third_party/zephyr/main/")[1]
        )
    elif path.is_absolute():
        if ec_root and path.is_relative_to(ec_root):
            path = path.relative_to(ec_root)
        elif zephyr_root and path.is_relative_to(zephyr_root):
            path = Path("zephyr-base") / path.relative_to(zephyr_root)

    path_str = str(path)
    path_str = re.sub(r"build/zephyr/[\w-]+/modules/ec/", "", path_str)
    path_str = re.sub(r"build/zephyr/[\w-]+/build-r[ow]/", "", path_str)

    return Path(path_str)


def find_unused_functions(ec_root, zephyr_root, source_paths, build_paths):
    """Find functions that are defined but never appear in ELFs."""
    print(f"Finding all defined functions in {len(source_paths)} paths...")
    all_functions = find_all_defined_functions(paths=source_paths)
    all_functions = {
        (normalize_path(ec_root, zephyr_root, f[0]), f[1])
        for f in all_functions
    }
    print(f"* Found {len(all_functions)} defined functions")

    print(f"Finding elf files in {len(build_paths)} paths...")
    all_elfs = list(find_files(paths=build_paths, patterns=["*.elf", "*.exe"]))
    print(f"* Found {len(all_elfs)} elf files")

    print("Extracting functions from elf files...")
    used_functions = set()
    count = 0
    with ProcessPoolExecutor(max_workers=MAX_WORKERS) as executor:
        futures = [
            executor.submit(find_functions_in_elf, elf) for elf in all_elfs
        ]
        for future in futures:
            count += 1
            elf_functions = future.result()
            used_functions.update(
                {
                    (normalize_path(ec_root, zephyr_root, f[0]), f[1])
                    for f in elf_functions
                }
            )
            print(f"{count}/{len(all_elfs)}", end="\r")
    print(f"\n* Extracted {len(used_functions)} unique functions from elfs")

    unused_functions = all_functions - used_functions
    print(f"\nUNUSED FUNCTIONS({len(unused_functions)}):\n")
    for path, function_name in sorted(list(unused_functions)):
        print(f"{str(path)}: {function_name}")


def find_unused_configs(source_paths, build_paths):
    """Find configs that are referenced in source but never set in builds."""
    print("Finding all referenced configs...")
    all_configs = find_configs(source_paths)
    print(f"* Found {len(all_configs)} configs")

    print("Finding all set configs...")
    set_configs = find_set_configs(build_paths)
    print(f"* Found {len(set_configs)} set configs")

    never_set_configs = all_configs - set_configs
    print(f"\nNEVER SET CONFIGS({len(never_set_configs)}):\n")
    for config in sorted(list(never_set_configs)):
        print(config)


def find_unused_sources(ec_root, zephyr_root, source_paths, build_paths):
    """Find source files that are never compiled into any ELF."""
    print(f"Finding all source files in {len(source_paths)} paths...")
    all_sources = find_files(
        paths=source_paths, patterns=["*.c", "*.h", "*.cc", "*.S", "*.inc"]
    )
    all_sources = {normalize_path(ec_root, zephyr_root, s) for s in all_sources}
    print(f"* Found {len(all_sources)} source files")

    print(f"Finding elf files in {len(build_paths)} paths...")
    all_elfs = find_files(paths=build_paths, patterns=["*.elf", "*.exe"])
    print(f"* Found {len(all_elfs)} elf files")

    print("Extracting sources from elf files...")
    used_sources = set()
    count = 0
    with ProcessPoolExecutor(max_workers=MAX_WORKERS) as executor:
        futures = [executor.submit(sources_from_elf, elf) for elf in all_elfs]
        for future in futures:
            count += 1
            elf_sources = future.result()
            used_sources.update(
                {normalize_path(ec_root, zephyr_root, s) for s in elf_sources}
            )
            print(f"{count}/{len(all_elfs)}", end="\r")

    print(f"\n* Extracted {len(used_sources)} unique source files from elfs")

    unused_sources = []
    for source in sorted(list(all_sources)):
        # Can't reliably detect unused header files using this method
        if source.suffix in [".h"]:
            continue
        if source not in used_sources:
            unused_sources.append(source)

    print(f"\nUNUSED SOURCES({len(unused_sources)}):\n")
    for source in unused_sources:
        print(source)


def find_unused_headers(ec_root, zephyr_root, source_paths, build_paths):
    """Find header files that are never included anywhere."""
    print(f"Finding all header files in {len(source_paths)} paths...")
    all_headers = find_files(paths=source_paths, patterns=["*.h", "*.inc"])
    all_headers = {normalize_path(ec_root, zephyr_root, s) for s in all_headers}
    print(f"* Found {len(all_headers)} header files")

    print(f"Finding elf files in {len(build_paths)} paths...")
    all_elfs = find_files(paths=build_paths, patterns=["*.elf", "*.exe"])
    print(f"* Found {len(all_elfs)} elf files")

    print("Extracting headers from elf files (DWARF heuristic)...")
    used_headers_dwarf = set()
    count = 0
    with ProcessPoolExecutor(max_workers=MAX_WORKERS) as executor:
        futures = [executor.submit(sources_from_elf, elf) for elf in all_elfs]
        for future in futures:
            count += 1
            elf_sources = future.result()
            used_headers_dwarf.update(
                {
                    normalize_path(ec_root, zephyr_root, s)
                    for s in elf_sources
                    if s.suffix in [".h", ".inc"]
                }
            )
            print(f"{count}/{len(all_elfs)}", end="\r")

    print(f"\n* DWARF info reported {len(used_headers_dwarf)} headers as used")

    print("Searching for #include directives (Grep heuristic)...")
    used_headers_grep = set()
    for path in source_paths:
        cmd = [
            "grep",
            "--recursive",
            "--no-filename",
            "--binary-files=without-match",
            "--only-matching",
            "--extended-regexp",
            r'#include\s*[<"][^>"]+[>"]',
            str(path),
        ]
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            check=False,
        )
        for line in result.stdout.splitlines():
            match = re.search(r'[<"]([^>"]+)[>"]', line)
            if match:
                include_path = match.group(1)
                used_headers_grep.add(Path(include_path).name)

    print(f"* Found {len(used_headers_grep)} unique filenames in includes")

    unused_headers = []
    for header in sorted(list(all_headers)):
        if header in used_headers_dwarf:
            continue
        if header.name in used_headers_grep:
            continue
        unused_headers.append(header)

    print(f"\nUNUSED HEADERS({len(unused_headers)}):\n")
    for header in unused_headers:
        print(header)


def main():
    """Main entry point."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "command", choices=["sources", "configs", "functions", "headers"]
    )
    parser.add_argument("--ec-root", type=Path, help="Path to EC root")
    parser.add_argument("--zephyr-root", type=Path, help="Path to Zephyr root")
    parser.add_argument("--board", help="Filter by board name (e.g. 'taniks')")
    parser.add_argument(
        "--include-tests", action="store_true", help="Include test directories"
    )
    args = parser.parse_args()

    if args.ec_root:
        ec_root = args.ec_root.resolve()
    else:
        ec_root = Path(__file__).resolve().parent.parent

    # Try to find zephyr root if not provided
    if args.zephyr_root:
        zephyr_root = args.zephyr_root.resolve()
    else:
        # Heuristic for Zephyr root in chromiumos tree
        zephyr_root = (
            ec_root.parent / "third_party" / "zephyrproject" / "zephyr"
        )
        if not zephyr_root.exists():
            zephyr_root = Path("/mnt/host/source/src/third_party/zephyr/main/")

    ec_zephyr_root = ec_root / "zephyr"
    source_paths = [
        ec_root / "baseboard",
        ec_root / "board",
        ec_root / "builtin",
        ec_root / "chip",
        ec_root / "common",
        ec_root / "core",
        ec_root / "crypto",
        ec_root / "driver",
        ec_root / "extra",
        ec_root / "include",
        ec_root / "libc",
        ec_root / "power",
        ec_root / "fuzz",
        ec_zephyr_root / "app",
        ec_zephyr_root / "boards",
        ec_zephyr_root / "chip",
        ec_zephyr_root / "drivers",
        ec_zephyr_root / "dts",
        ec_zephyr_root / "include",
        ec_zephyr_root / "lib",
        ec_zephyr_root / "libc",
        ec_zephyr_root / "program",
        ec_zephyr_root / "shim",
        ec_zephyr_root / "subsys",
    ]

    test_source_paths = [
        ec_root / "test",
        ec_zephyr_root / "test",
        ec_zephyr_root / "fake",
        ec_zephyr_root / "emul",
    ]

    if args.include_tests:
        source_paths.extend(test_source_paths)

    source_paths = [p for p in source_paths if p.exists()]

    if args.board:
        build_paths = [ec_root / "build" / args.board]
    else:
        build_paths = [ec_root / "build"]

    build_paths = [p for p in build_paths if p.exists()]

    if not source_paths:
        print(f"Error: No source paths found in {ec_root}")
        sys.exit(1)

    match args.command:
        case "sources":
            find_unused_sources(ec_root, zephyr_root, source_paths, build_paths)
        case "configs":
            find_unused_configs(source_paths, build_paths)
        case "functions":
            find_unused_functions(
                ec_root, zephyr_root, source_paths, build_paths
            )
        case "headers":
            find_unused_headers(ec_root, zephyr_root, source_paths, build_paths)


if __name__ == "__main__":
    main()
