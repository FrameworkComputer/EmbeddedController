# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Module to compare Zephyr EC builds"""

import dataclasses
import logging
import os
import pathlib
import subprocess
import sys

from zmake.output_packers import packer_registry


def get_git_hash(ref):
    """Get the full git commit hash for a git reference

    Args:
        ref: Git reference (e.g. HEAD, m/main, sha256)

    Returns:
        A string, with the full hash of the git reference
    """

    try:
        result = subprocess.run(
            ["git", "rev-parse", ref],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            encoding="utf-8",
        )
    except subprocess.CalledProcessError:
        logging.error("Failed to determine hash for git reference %s", ref)
        sys.exit(1)
    else:
        full_reference = result.stdout.strip()

    return full_reference


def git_do_checkout(module_name, work_dir, git_source, dst_dir, git_ref):
    """Clone a repository and perform a checkout.

    Args:
        module_name: The module name to checkout.
        work_dir: Root directory for the checktout.
        git_source: Path to the repository for the module.
        dst_dir: Destination directory for the checkout, relative to the work_dir.
        git_ref: Git reference to checkout.
    """
    cmd = ["git", "clone", "--quiet", "--no-checkout", git_source, dst_dir]

    try:
        subprocess.run(
            cmd,
            cwd=work_dir,
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    except subprocess.CalledProcessError:
        logging.error("Clone failed for %s", module_name)
        sys.exit(1)

    cmd = ["git", "-C", dst_dir, "checkout", "--quiet", git_ref]
    try:
        subprocess.run(
            cmd,
            cwd=work_dir,
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    except subprocess.CalledProcessError:
        logging.error("Checkout of %s failed for %s", git_ref, module_name)
        sys.exit(1)


def create_bin_from_elf(elf_input, bin_output):
    """Create a plain binary from an ELF executable

    Args:
        elf_input - ELF output file, created by zmake
        bin_output - Output binary filename. Created by this function.
    """

    cmd = ["objcopy", "-O", "binary"]
    # Some native-posix builds include a GNU build ID, which is guaranteed
    # unique from build to build. Remove this section during conversion
    # binary format.
    cmd.extend(["-R", ".note.gnu.build-id"])
    cmd.extend([elf_input, bin_output])
    try:
        subprocess.run(
            cmd,
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    except subprocess.CalledProcessError:
        logging.error("Failed to create binary: %s", bin_output)
        sys.exit(1)


@dataclasses.dataclass
class CheckoutConfig:
    """All the information needed to build the EC at a specific checkout."""

    temp_dir: str
    ref: str
    full_ref: str = dataclasses.field(default_factory=str)
    work_dir: pathlib.Path = dataclasses.field(default_factory=pathlib.Path)
    zephyr_dir: pathlib.Path = dataclasses.field(default_factory=pathlib.Path)
    modules_dir: pathlib.Path = dataclasses.field(default_factory=pathlib.Path)

    def __post_init__(self):
        self.full_ref = get_git_hash(self.ref)
        self.work_dir = pathlib.Path(self.temp_dir) / self.full_ref
        self.zephyr_dir = self.work_dir / "zephyr-base"
        self.modules_dir = self.work_dir / "modules"

        os.mkdir(self.work_dir)


class CompareBuilds:
    """Information required to build Zephyr EC projects at a specific EC git
        commit reference.

    Args:
        temp_dir: Temporary directory where all sources will be checked out
            and built.
        ref1: 1st git reference for the EC repository.  May be a partial hash,
            local branch name, or remote branch name.
        ref2: 2nd git reference for the EC repository.

    Attributes:
        checkouts: list of CheckoutConfig objects containing information
            about the code checkout at each EC git reference.
    """

    def __init__(self, temp_dir, ref1, ref2):
        self.checkouts = []
        self.checkouts.append(CheckoutConfig(temp_dir, ref1))
        self.checkouts.append(CheckoutConfig(temp_dir, ref2))

    def do_checkouts(self, zephyr_base, module_paths):
        """Checkout all EC sources at a specific commit.

        Args:
            zephyr_base: The location of the zephyr sources.
            module_paths: The location of the module sources.
        """

        for checkout in self.checkouts:
            for module_name, git_source in module_paths.items():
                dst_dir = checkout.modules_dir / module_name
                git_ref = checkout.full_ref if module_name == "ec" else "HEAD"
                git_do_checkout(
                    module_name=module_name,
                    work_dir=checkout.work_dir,
                    git_source=git_source,
                    dst_dir=dst_dir,
                    git_ref=git_ref,
                )

            git_do_checkout(
                module_name="zephyr",
                work_dir=checkout.work_dir,
                git_source=zephyr_base,
                dst_dir="zephyr-base",
                git_ref="HEAD",
            )

    def check_binaries(self, projects):
        """Compare Zephyr EC binaries for two different source trees

        Args:
            projects: List of projects to compare the output binaries.

        Returns:
            A list of projects that failed to compare.  An empty list indicates that
            all projects compared successfully.
        """

        failed_projects = []
        for project in projects:
            if project.config.is_test:
                continue

            output_path = (
                pathlib.Path("ec")
                / "build"
                / "zephyr"
                / pathlib.Path(project.config.project_name)
                / "output"
            )

            output_dir1 = self.checkouts[0].modules_dir / output_path
            output_dir2 = self.checkouts[1].modules_dir / output_path

            bin_output1 = output_dir1 / "ec.bin"
            bin_output2 = output_dir2 / "ec.bin"

            # ELF executables don't compare due to meta data.  Convert to a binary
            # for the comparison
            if project.config.output_packer == packer_registry["elf"]:
                create_bin_from_elf(
                    elf_input=output_dir1 / "zephyr.elf", bin_output=bin_output1
                )
                create_bin_from_elf(
                    elf_input=output_dir2 / "zephyr.elf", bin_output=bin_output2
                )

            bin1_path = pathlib.Path(bin_output1)
            bin2_path = pathlib.Path(bin_output2)
            if not os.path.isfile(bin1_path) or not os.path.isfile(bin2_path):
                failed_projects.append(project.config.project_name)
                logging.error(
                    "Zephyr EC binary not found for project %s",
                    project.config.project_name,
                )
                continue

            try:
                subprocess.run(
                    ["cmp", bin_output1, bin_output2],
                    check=True,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                )
            except subprocess.CalledProcessError:
                failed_projects.append(project.config.project_name)

        return failed_projects
