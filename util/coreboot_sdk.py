#!/usr/bin/env -S python3 -u
# -*- coding: utf-8 -*-
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Provide coreboot-sdk to callers

Initialize the coreboot-sdk subtools and provide environment variables
to the caller to indicate the extracted location.
"""

import os
import shutil
import subprocess
import sys
import tempfile
from typing import Dict, Tuple, Union
import urllib.request


script_dir = os.path.dirname(os.path.abspath(__file__))
if script_dir not in sys.path:
    sys.path.insert(0, script_dir)

# pylint: disable=import-error, wrong-import-position
from coreboot_sdk_portage_deps import get_portage_deps


toolchain_name_map = {
    "arm-eabi": "COREBOOT_SDK_ROOT_arm",
    "picolibc-arm-eabi": "COREBOOT_SDK_ROOT_picolibc_arm",
    "libstdcxx-arm-eabi": "COREBOOT_SDK_ROOT_libstdcxx_arm",
    "i386-elf": "COREBOOT_SDK_ROOT_x86",
    "picolibc-i386-elf": "COREBOOT_SDK_ROOT_picolibc_x86",
    "libstdcxx-i386-elf": "COREBOOT_SDK_ROOT_libstdcxx_x86",
    "riscv-elf": "COREBOOT_SDK_ROOT_riscv",
    "riscv64-elf": "COREBOOT_SDK_ROOT_riscv64",
    "picolibc-riscv64-elf": "COREBOOT_SDK_ROOT_picolibc_riscv64",
    "libstdcxx-riscv64-elf": "COREBOOT_SDK_ROOT_libstdcxx_riscv64",
    "nds32le-elf": "COREBOOT_SDK_ROOT_nds32",
}


def get_toolchains_shell(
    portage_toolchains: Dict[str, Tuple],
    local_filepath: Union[str, "os.PathLike[str]"] = os.path.expanduser(
        "~/.cache/coreboot-sdk"
    ),
) -> Dict[str, str]:
    """Download and extract the toolchains using the shell.

    Args:
        portage_toolchains: Dict of architectures to download
        local_filepath: Path to download the toolchains to

    Returns:
        Dict of coreboot-sdk env variables and their respective paths
    """
    result = {}
    success = True
    for target, (
        gcs_bucket,
        version,
        toolchain_hash,
    ) in portage_toolchains.items():
        output_path = local_filepath + "/" + target
        output_toolchain = output_path + "/" + toolchain_hash
        tempfile.tempdir = output_path
        os.makedirs(output_path, exist_ok=True)
        with tempfile.TemporaryDirectory(toolchain_hash) as tmp_dir:
            downloaded_file = tmp_dir + ".tar.zst"
            if os.path.exists(output_toolchain) and os.path.isdir(
                output_toolchain
            ):
                print(
                    f"Skipping {downloaded_file} because the output dir exists",
                    file=sys.stderr,
                )
            else:
                src_uri = (
                    f"https://storage.googleapis.com/{gcs_bucket}/toolchains/coreboot-sdk"
                    f"-{target}/{version}/{toolchain_hash}.tar.zst"
                )

                try:
                    with urllib.request.urlopen(src_uri) as source_file, open(
                        downloaded_file, "wb"
                    ) as dest_file:
                        shutil.copyfileobj(source_file, dest_file)
                except urllib.error.HTTPError as e:
                    print(
                        f"HTTP Error: {e.code} - {e.reason}: {src_uri}",
                        file=sys.stderr,
                    )
                    success = False
                    continue
                except TimeoutError as e:
                    print(f"Timeout Error: {e}", file=sys.stderr)
                    success = False
                    continue
                except ConnectionRefusedError as e:
                    print(f"Connection refused: {e}", file=sys.stderr)
                    success = False
                    continue
                except urllib.error.URLError as e:
                    print(f"URL Error: {e.reason}", file=sys.stderr)
                    success = False
                    continue
                except shutil.SameFileError:
                    print(
                        "The source and dest files are the same",
                        file=sys.stderr,
                    )
                    success = False
                    continue
                except FileNotFoundError:
                    print(
                        f"Error: File not found '{downloaded_file}'",
                        file=sys.stderr,
                    )
                    success = False
                    continue
                except OSError as e:
                    print(f"OS Error: {e}", file=sys.stderr)
                    success = False
                    continue

                try:
                    subprocess.run(
                        ["tar", "-xf", downloaded_file, "-C", tmp_dir],
                        check=True,
                    )
                    shutil.move(tmp_dir, output_toolchain)
                    print(
                        f"Successfully extracted '{downloaded_file}'"
                        f" to '{output_toolchain}'",
                        file=sys.stderr,
                    )
                except subprocess.CalledProcessError as e:
                    print(
                        f"Error extracting with command-line tool: {e}",
                        file=sys.stderr,
                    )
                    success = False
                    continue

            result[
                toolchain_name_map.get(target) or f"COREBOOT_SDK_ROOT_{target}"
            ] = output_toolchain
    if not success:
        raise FileNotFoundError(
            "Failed to download the found toolchains.  Please check the log"
        )
    return result


def init_toolchain() -> Dict[str, str]:
    """Initialize coreboot-sdk.

    Returns:
        Environment variables to use for toolchain.
    """
    if os.environ.get("COREBOOT_SDK_ROOT") is not None:
        print("COREBOOT_SDK_ROOT already set by environment, returning")
        return {}

    portage_toolchains = get_portage_deps()

    return get_toolchains_shell(portage_toolchains)


def main():
    """Main calling function for the script"""
    env_vars = init_toolchain()
    # Return a formatted string which can be declared as an associative array in bash
    if env_vars:
        print(
            " ".join(f'["{key}"]="{value}"' for key, value in env_vars.items())
        )


if __name__ == "__main__":
    main()
