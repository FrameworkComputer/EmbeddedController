# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Module to compare Zephyr EC builds"""

import dataclasses
import logging
import os
import pathlib
import shlex
import subprocess
import sys
from typing import List

import zmake.modules
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
    cmd = [
        "git",
        "clone",
        "--quiet",
        "--no-checkout",
        "file://" + str(git_source),
        str(dst_dir),
    ]

    try:
        subprocess.run(
            cmd,
            cwd=work_dir,
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    except subprocess.CalledProcessError:
        logging.error("Clone failed for %s: %s", module_name, shlex.join(cmd))
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
        logging.error(
            "Checkout of %s failed for %s: %s",
            git_ref,
            module_name,
            shlex.join(cmd),
        )
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


def _compare_non_test_projects(projects, cmp_method, *args):
    failed_projects = []
    for project in projects:
        if not cmp_method(project, *args):
            failed_projects.append(project.config.project_name)

    return failed_projects


@dataclasses.dataclass
class CheckoutConfig:
    """All the information needed to build the EC at a specific checkout."""

    temp_dir: str
    ref: str
    full_ref: str = dataclasses.field(default_factory=str)
    work_dir: pathlib.Path = dataclasses.field(default_factory=pathlib.Path)
    zephyr_dir: pathlib.Path = dataclasses.field(default_factory=pathlib.Path)
    modules_dir: pathlib.Path = dataclasses.field(default_factory=pathlib.Path)
    projects_dirs: List[pathlib.Path] = dataclasses.field(default_factory=list)

    def __post_init__(self):
        self.full_ref = get_git_hash(self.ref)
        self.work_dir = pathlib.Path(self.temp_dir) / self.full_ref
        self.zephyr_dir = self.work_dir / "zephyr-base"
        self.modules_dir = self.work_dir / "modules"
        modules = {
            key: self.modules_dir / key for key in zmake.modules.known_modules
        }
        self.projects_dirs = zmake.modules.default_projects_dirs(modules)

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

    def _compare_binaries(self, project):
        output_path = (
            pathlib.Path("ec")
            / "build"
            / "zephyr"
            / pathlib.Path(project.config.project_name)
            / "output"
        )

        output_dir1 = self.checkouts[0].modules_dir / output_path
        output_dir2 = self.checkouts[1].modules_dir / output_path

        # The rex-ish target creates an ish_fw.bin artifact instead of ec.bin
        if project.config.project_name == "rex-ish":
            bin_name = "ish_fw.bin"
        else:
            bin_name = "ec.bin"

        bin_output1 = output_dir1 / bin_name
        bin_output2 = output_dir2 / bin_name

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
            logging.error(
                "Zephyr binary '%s' not found for project %s",
                bin_name,
                project.config.project_name,
            )
            return False

        try:
            subprocess.run(
                ["cmp", bin_output1, bin_output2],
                check=True,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
        except subprocess.CalledProcessError:
            return False
        return True

    def check_binaries(self, projects):
        """Compare Zephyr EC binaries for two different source trees

        Args:
            projects: List of projects to compare the output binaries.

        Returns:
            A list of projects that failed to compare.  An empty list indicates that
            all projects compared successfully.
        """

        failed_projects = _compare_non_test_projects(
            projects, self._compare_binaries
        )
        return failed_projects

    def _compare_build_files(self, project, build_mode, file):
        build_path = (
            pathlib.Path("ec")
            / "build"
            / "zephyr"
            / pathlib.Path(project.config.project_name)
            / f"build-{build_mode}"
            / "zephyr"
        )

        build_dir1 = self.checkouts[0].modules_dir / build_path
        build_dir2 = self.checkouts[1].modules_dir / build_path

        file1 = build_dir1 / file
        file2 = build_dir2 / file

        try:
            data1 = ""
            data2 = ""
            with open(file1, encoding="utf-8") as fp1, open(
                file2, encoding="utf-8"
            ) as fp2:
                data1 = fp1.read()
                data2 = fp2.read()
            data1 = data1.replace(self.checkouts[0].full_ref, "")
            data2 = data2.replace(self.checkouts[1].full_ref, "")
            return data1 == data2
        except FileNotFoundError as err:
            logging.error(
                "Zephyr build-%s %s file not found for project %s: %s",
                build_mode,
                file,
                project.config.project_name,
                err,
            )
        return False

    def _check_build_files(self, project, file):
        return self._compare_build_files(
            project, "ro", file
        ) and self._compare_build_files(project, "rw", file)

    def check_configs(self, projects):
        """Compare Zephyr EC Config files for two different source trees

        Args:
            projects: List of projects to compare the .config files.

        Returns:
            A list of projects that failed to compare.  An empty list indicates that
            all projects compared successfully.
        """

        failed_projects = _compare_non_test_projects(
            projects,
            self._check_build_files,
            ".config",
        )
        return failed_projects

    def check_devicetrees(self, projects):
        """Compare Zephyr EC devicetree files for two different source trees

        Args:
            projects: List of projects to compare the zephyr.dts files.

        Returns:
            A list of projects that failed to compare.  An empty list indicates that
            all projects compared successfully.
        """

        failed_projects = _compare_non_test_projects(
            projects,
            self._check_build_files,
            "zephyr.dts",
        )
        return failed_projects
