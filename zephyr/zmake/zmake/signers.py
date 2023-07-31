# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Utilities for signing. """

import abc
import logging
import os.path
from pathlib import Path
import subprocess
from typing import Iterable, List, Tuple, Union

from zmake import jobserver
from zmake import multiproc
from zmake import util


class BaseSigner(abc.ABC):  # pylint: disable=too-few-public-methods
    """Abstract base for signers."""

    @abc.abstractmethod
    def sign(
        self,
        files: Iterable[Tuple[Path, str]],
        work_dir: Path,
        jobclient: jobserver.JobClient,
    ) -> Iterable[Tuple[Path, str]]:
        """Sign a firmware image.

        Args:
            files: An iterable of 2-tuples (output_path, output_name) of
                   unsigned artifacts produced by output packer and their name.
            work_dir: A directory to write outputs and temporary files into.
            jobclient: A JobClient object to use.

        Returns:
            An iterable of 2-tuples (output_path, output_name) which should be
            copied into the output directory, and the output filename.
        """
        raise NotImplementedError


class NullSigner(BaseSigner):  # pylint: disable=too-few-public-methods
    """A signer that does nothing."""

    def sign(self, files, work_dir, jobclient):
        return files


class RwsigSigner(BaseSigner):
    """Signer to sign a image using rwsig algorithm.

    This expects that the image is packed using FMAP, the image contains a
    KEY_RO section in its RO region to hold the public key (vb21_packed_key
    format), a SIG_RW section at the bottom of EC_RW region to hold the
    signature (vb21_signature format).

    This signer generates a signature for the range EC_RW minus SIG_RW.
    """

    def __init__(self, key: Path):
        """Constructor.

        Arg:
            key: Path to a RSA key in PEM format.
        """
        self.key = key
        self.logger = logging.getLogger(self.__class__.__name__)

    def sign(
        self,
        files: Iterable[Tuple[Path, str]],
        work_dir: Path,
        jobclient: jobserver.JobClient,
    ) -> Iterable[Tuple[Path, str]]:
        """Sign a firmware image using rwsig algorithm.

        Args:
            files: An iterable of 2-tuples (output_path, output_name) of
                   unsigned artifacts produced by output packer and their name.
            work_dir: A directory to write outputs and temporary files into.
            jobclient: A JobClient object to use.

        Returns:
            An iterable of 2-tuples (output_name, output_path) which should be
            copied into the output directory, and the output filename.
        """
        for path, name in files:
            if name == "ec.bin":
                yield self.sign_ec(path, work_dir, jobclient), name
            else:
                yield path, name

    def sign_ec(
        self,
        bin_file: Path,
        work_dir: Path,
        jobclient: jobserver.JobClient,
    ) -> Path:
        """Sign a EC binary.

        Args:
            bin_file: Path to the unsigned EC binary.
            work_dir: A directory to write outputs and temporary files into.
            jobclient: A JobClient object to use.

        Returns:
            Path to the signed firmware.
        """
        ec_rw = work_dir / "ec_rw"
        pub_key = work_dir / "key.vbpubk2"
        pri_key = work_dir / "key.vbprik2"
        sig_file = work_dir / "ec.sig"
        signed_bin = work_dir / "ec-signed.bin"

        self._run_futility(
            [
                "dump_fmap",
                "-x",
                bin_file,
                f"EC_RW:{ec_rw}",
                f"SIG_RW:{sig_file}",
            ],
            work_dir,
            jobclient,
        )
        data_size = os.path.getsize(ec_rw) - os.path.getsize(sig_file)

        self._run_futility(
            ["create", self.key, work_dir / "key"], work_dir, jobclient
        )

        self._run_futility(
            [
                "sign",
                "--type",
                "rwsig",
                "--data_size",
                str(data_size),
                "--prikey",
                pri_key,
                ec_rw,
                sig_file,
            ],
            work_dir,
            jobclient,
        )

        self._run_futility(
            [
                "load_fmap",
                "-o",
                signed_bin,
                bin_file,
                f"KEY_RO:{pub_key}",
                f"SIG_RW:{sig_file}",
            ],
            work_dir,
            jobclient,
        )

        return signed_bin

    def _run_futility(
        self,
        args: List[Union[str, Path]],
        work_dir: Path,
        jobclient: jobserver.JobClient,
    ) -> None:
        """Helper to execute futility command.

        Args:
            args: Command-line arguments.
            work_dir: A directory to write outputs and temporary files into.
            jobclient: A JobClient object to use.
        """
        proc = jobclient.popen(
            [util.get_tool_path("futility"), *args],
            cwd=work_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            encoding="utf-8",
        )
        multiproc.LogWriter.log_output(self.logger, logging.DEBUG, proc.stdout)
        multiproc.LogWriter.log_output(self.logger, logging.ERROR, proc.stderr)
        proc.wait(timeout=60)
        if proc.returncode:
            raise subprocess.CalledProcessError(proc.returncode, proc.args)
