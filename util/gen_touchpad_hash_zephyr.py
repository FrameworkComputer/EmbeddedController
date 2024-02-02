#!/usr/bin/env python3
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Utility script to inject touchpad firmware hash into EC firmware."""

import argparse
import hashlib
from pathlib import Path
import re
import subprocess
import tempfile
from typing import Tuple


SHA256_DIGEST_LENGTH = 32


def calculate_update_pdu_size(ec_fw: Path, tp_fw: Path) -> int:
    """Calculate the UPDATE_PDU_SIZE

    This function assumes EC correctly allocates the TOUCHPAD_FW_HASHES section,
    size of TOUCHPAD_FW_HASHES must equal to (number of blocks * 32).

    Args:
      ec_fw: Path to EC firmware
      tp_fw: Path to touchpad firmware

    Returns:
      Computed UPDATE_PDU_SIZE.
    """

    proc = subprocess.run(
        ["futility", "dump_fmap", "-p", ec_fw, "TOUCHPAD_FW_HASHES"],
        check=True,
        capture_output=True,
    )
    # output will be something like:
    #   TOUCHPAD_FW_HASHES 350648 2048
    if match := re.match(rb"TOUCHPAD_FW_HASHES \d+ (\d+)", proc.stdout):
        sec_size = int(match.group(1))

    tp_fw_size = tp_fw.stat().st_size
    num_blocks = sec_size // SHA256_DIGEST_LENGTH

    return tp_fw_size // num_blocks


def calculate_hashes(tp_fw: Path, update_pdu_size: int) -> Tuple[bytes, bytes]:
    """Calculate the sha256 hash of touchpad firmware.

    Args:
      tp_fw: Path to touchpad firmware
      update_pdu_size:

    Returns:
      A 2-tuple consists of
        1) SHA256 digest of every `update_pdu_size` bytes chunk.
        2) The SHA256 digest of the whole firmware.
    """

    full_hash = hashlib.sha256()
    chunk_hashes = b""
    with open(tp_fw, "rb") as tp_file:
        while chunk := tp_file.read(update_pdu_size):
            chunk_hashes += hashlib.sha256(chunk).digest()
            full_hash.update(chunk)

    return chunk_hashes, full_hash.digest()


def main():
    """Main function."""

    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--ec-fw", type=Path, required=True, help="Path to the EC firmware"
    )
    parser.add_argument(
        "--prikey",
        type=Path,
        required=True,
        help="Path to the private signing key (*.vbprik2)",
    )
    parser.add_argument(
        "--tp-fw",
        type=Path,
        required=True,
        help="Path to the touchpad firmware",
    )

    args = parser.parse_args()

    update_pdu_size = calculate_update_pdu_size(args.ec_fw, args.tp_fw)

    tp_hashes, tp_full_hash = calculate_hashes(args.tp_fw, update_pdu_size)

    # inject touchpad hashes
    with tempfile.TemporaryDirectory() as work_dir:
        work_dir_path = Path(work_dir)

        temp_tp_hashes = work_dir_path / "tp_hashes"
        temp_tp_hashes.write_bytes(tp_hashes)

        temp_tp_full_hash = work_dir_path / "tp_full_hash"
        temp_tp_full_hash.write_bytes(tp_full_hash)

        subprocess.check_call(
            [
                "futility",
                "load_fmap",
                args.ec_fw,
                f"TOUCHPAD_FW_HASHES:{temp_tp_hashes}",
                f"TOUCHPAD_FW_FULL_HASH:{temp_tp_full_hash}",
            ],
        )

    # resign firmware
    subprocess.check_call(
        [
            "futility",
            "sign",
            "--type",
            "rwsig",
            args.ec_fw,
            "--prikey",
            args.prikey,
        ]
    )


if __name__ == "__main__":
    main()
