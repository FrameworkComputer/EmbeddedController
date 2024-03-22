# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Tests for signers."""

from pathlib import Path
from unittest import mock

# pylint: disable=import-error
from zmake import signers


@mock.patch.object(signers.RwsigSigner, "_run_futility")
def test_rwsig_sign(mock_futility, tmpdir):
    """Test rwsig signing.

    We can't call futility here, so this test only verifies the
    number of bytes to sign is good.
    """

    signer = signers.RwsigSigner(Path())

    # fake files: 1000 bytes EC_RW and 100 bytes signature
    with open(tmpdir / "ec_rw", "wb") as ec_rw:
        ec_rw.write(b"\0" * 1000)
    with open(tmpdir / "ec.sig", "wb") as sig:
        sig.write(b"\0" * 100)

    packer_output = [
        (Path("a"), "ec.bin"),
        (Path("b"), "zephyr.ro.elf"),
        (Path("c"), "zephyr.rw.elf"),
    ]

    signed = list(signer.sign(packer_output, tmpdir, None))

    mock_futility.assert_any_call(
        ["sign", "--type", "rwsig", "--data_size", "900"] + [mock.ANY] * 4,
        mock.ANY,
        mock.ANY,
    )

    # expect only path to ec.bin changed.
    assert signed[0][1] == "ec.bin"
    assert signed[1][1] == "key.vbprik2"
    assert signed[2] == (Path("b"), "zephyr.ro.elf")
    assert signed[3] == (Path("c"), "zephyr.rw.elf")
