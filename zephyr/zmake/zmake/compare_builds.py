# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Module to compare Zephyr EC builds"""

import dataclasses
import functools
import logging
import os
import pathlib
import shlex
import subprocess
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
    except subprocess.CalledProcessError as e:
        raise OSError(
            f"Failed to determine hash for git reference {ref}"
        ) from e
    full_reference = result.stdout.strip()

    return full_reference


def _git_clone_repo(module_name, work_dir, git_source, dst_dir):
    """Clone a repository, skipping the checkout.

    Args:
        module_name: The module name to checkout.
        work_dir: Root directory for the checkout.
        git_source: Path to the repository for the module.
        dst_dir: Destination directory for the checkout, relative to work_dir.

    Returns:
        0 on success, non-zero otherwise
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
    except subprocess.CalledProcessError as e:
        raise OSError(
            f"Clone failed for {module_name}: {shlex.join(cmd)}"
        ) from e

    return 0


def _git_do_checkout(work_dir, dst_dir, git_ref):
    """Perform a checkout of a specific Git reference from existing repository.

    Args:
        work_dir: Root directory for the checkout.
        dst_dir: Destination directory for the checkout, relative to work_dir.
        git_ref: Git reference to checkout.

    Returns:
        0 on success, non-zero otherwise
    """
    cmd = ["git", "-C", dst_dir, "checkout", "--quiet", git_ref]
    try:
        subprocess.run(
            cmd,
            cwd=work_dir,
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    except subprocess.CalledProcessError as e:
        raise OSError(
            f"Checkout of {git_ref} failed for: {shlex.join(cmd)}"
        ) from e

    return 0


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
    except subprocess.CalledProcessError as e:
        raise OSError(f"Failed to create binary: {bin_output}") from e


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
        executor: a zmake.multiproc.Executor object for submitting
            tasks to.
        sequential: True to perform git checkouts sequentially. False to
            do the git checkouts in parallel.

    Attributes:
        checkouts: list of CheckoutConfig objects containing information
            about the code checkout at each EC git reference.
    """

    def __init__(self, temp_dir, ref1, ref2, executor, sequential):
        self.checkouts = []
        self.checkouts.append(CheckoutConfig(temp_dir, ref1))
        self.checkouts.append(CheckoutConfig(temp_dir, ref2))
        self._executor = executor
        self._sequential = sequential

    def _do_git_work(self, func):
        """Start a git operation. If the "sequential" option is disabled
            then the operation is started in the background and the caller
            must use the executor.wait routine.
            If "sequential" is enabled, then this routine blocks until the
            git operation completes.

        Args:
            func: A function that is passed to the multiproc executor.
        """
        self._executor.append(func)

        if self._sequential and self._executor.wait():
            # git operation is blocking, and returned non-zero.
            raise OSError("Failed to complete git work")

    def _do_git_wait(self, message):
        """Wait for any pending git operations.  This is a no-op if the
        sequential option is used.
        """
        if not self._sequential and self._executor.wait():
            raise OSError(message)

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
                self._do_git_work(
                    func=functools.partial(
                        _git_clone_repo,
                        module_name=module_name,
                        work_dir=checkout.work_dir,
                        git_source=git_source,
                        dst_dir=dst_dir,
                    )
                )

            self._do_git_work(
                func=functools.partial(
                    _git_clone_repo,
                    module_name="zephyr",
                    work_dir=checkout.work_dir,
                    git_source=zephyr_base,
                    dst_dir="zephyr-base",
                )
            )

        self._do_git_wait("Failed to clone one or more repositories")

        for checkout in self.checkouts:
            for module_name, git_source in module_paths.items():
                dst_dir = checkout.modules_dir / module_name
                git_ref = checkout.full_ref if module_name == "ec" else "HEAD"
                self._do_git_work(
                    func=functools.partial(
                        _git_do_checkout,
                        work_dir=checkout.work_dir,
                        dst_dir=dst_dir,
                        git_ref=git_ref,
                    )
                )

            self._do_git_work(
                func=functools.partial(
                    _git_do_checkout,
                    work_dir=checkout.work_dir,
                    dst_dir="zephyr-base",
                    git_ref="HEAD",
                )
            )

        self._do_git_wait("Failed to checkout one or more repositories")

    def _compare_binaries(self, project):
        output_path = (
            pathlib.Path("ec")
            / "build"
            / "zephyr"
            / pathlib.Path(project.config.project_name)
            / "output"
        )
        ish_targets = {
            "rex-ish",
            "brox-ish",
            "orisa-ish",
            "ptl-ish",
            "trulo-ish",
        }

        output_dir1 = self.checkouts[0].modules_dir / output_path
        output_dir2 = self.checkouts[1].modules_dir / output_path

        # The rex-ish and similar ish targets create an ish_fw.bin artifact
        # instead of ec.bin
        if project.config.project_name in ish_targets:
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
