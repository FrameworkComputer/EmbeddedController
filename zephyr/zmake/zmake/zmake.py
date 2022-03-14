# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Module encapsulating Zmake wrapper object."""
import difflib
import functools
import logging
import os
import pathlib
import re
import shutil
import subprocess
from typing import Dict, List

import zmake.build_config
import zmake.generate_readme
import zmake.jobserver
import zmake.modules
import zmake.multiproc
import zmake.project
import zmake.util as util
import zmake.version

ninja_warnings = re.compile(r"^(\S*: )?warning:.*")
ninja_errors = re.compile(r"error:.*")


def ninja_stdout_log_level_override(line, current_log_level):
    """Update the log level for ninja builds if we hit an error.

    Ninja builds prints everything to stdout, but really we want to start
    logging things to CRITICAL

    Args:
        line: The line that is about to be logged.
        current_log_level: The active logging level that would be used for the
          line.
    """
    # Output lines from Zephyr that are not normally useful
    # Send any lines that start with these strings to INFO
    cmake_suppress = [
        "-- ",  # device tree messages
        "Loaded configuration",
        "Including boilerplate",
        "Parsing ",
        "No change to configuration",
        "No change to Kconfig header",
    ]

    # Herewith a long list of things which are really for debugging, not
    # development. Return logging.DEBUG for each of these.

    # ninja puts progress information on stdout
    if line.startswith("["):
        return logging.DEBUG
    # we don't care about entering directories since it happens every time
    if line.startswith("ninja: Entering directory"):
        return logging.DEBUG
    # we know the build stops from the compiler messages and ninja return code
    if line.startswith("ninja: build stopped"):
        return logging.DEBUG
    # someone prints a *** SUCCESS *** message which we don't need
    if line.startswith("***"):
        return logging.DEBUG
    # dopey ninja puts errors on stdout, so fix that. It does not look
    # likely that it will be fixed upstream:
    # https://github.com/ninja-build/ninja/issues/1537
    # Try to drop output about the device tree
    if any(line.startswith(x) for x in cmake_suppress):
        return logging.INFO
    # this message is a bit like make failing. We already got the error output.
    if line.startswith("FAILED: CMakeFiles"):
        return logging.INFO
    # if a particular file fails it shows the build line used, but that is not
    # useful except for debugging.
    if line.startswith("ccache"):
        return logging.DEBUG
    if ninja_warnings.match(line):
        return logging.WARNING
    if ninja_errors.match(line):
        return logging.ERROR
    # When we see "Memory region" go into INFO, and stay there as long as the
    # line starts with \S+:
    if line.startswith("Memory region"):
        return logging.INFO
    if current_log_level == logging.INFO and line.split()[0].endswith(":"):
        return current_log_level
    if current_log_level == logging.WARNING:
        return current_log_level
    return logging.ERROR


def cmake_log_level_override(line, default_log_level):
    """Update the log level for cmake output if we hit an error.

    Cmake prints some messages that are less than useful during
    development.

    Args:
        line: The line that is about to be logged.
        default_log_level: The default logging level that will be used for the
          line.
    """
    # Strange output from Zephyr that we normally ignore
    if line.startswith("Including boilerplate"):
        return logging.DEBUG
    elif line.startswith("devicetree error:"):
        return logging.ERROR
    if ninja_warnings.match(line):
        return logging.WARNING
    if ninja_errors.match(line):
        return logging.ERROR
    return default_log_level


def get_process_failure_msg(proc):
    """Creates a suitable failure message if something exits badly

    Args:
        proc: subprocess.Popen object containing the thing that failed

    Returns:
        Failure message as a string:
    """
    return "Execution failed (return code={}): {}\n".format(
        proc.returncode, util.repr_command(proc.args)
    )


class Zmake:
    """Wrapper class encapsulating zmake's supported operations.

    The invocations of the constructor and the methods actually comes
    from the main function.  The command line arguments are translated
    such that dashes are replaced with underscores and applied as
    keyword arguments to the constructor and the method, and the
    subcommand invoked becomes the method run.

    As such, you won't find documentation for each method's parameters
    here, as it would be duplicate of the help strings from the
    command line.  Run "zmake --help" for full documentation of each
    parameter.

    Properties:
        executor: a zmake.multiproc.Executor object for submitting
            tasks to.
        _sequential: True to check the results of each build job sequentially,
            before launching more, False to just do this after all jobs complete
    """

    def __init__(
        self,
        checkout=None,
        jobserver=None,
        jobs=0,
        goma=False,
        gomacc="/mnt/host/depot_tools/.cipd_bin/gomacc",
        modules_dir=None,
        zephyr_base=None,
    ):
        zmake.multiproc.reset()
        self.logger = logging.getLogger(self.__class__.__name__)
        self._checkout = checkout
        self.goma = goma
        self.gomacc = gomacc
        if zephyr_base:
            self.zephyr_base = zephyr_base
        else:
            self.zephyr_base = self.checkout / "src" / "third_party" / "zephyr" / "main"

        if modules_dir:
            self.module_paths = zmake.modules.locate_from_directory(modules_dir)
        else:
            self.module_paths = zmake.modules.locate_from_checkout(self.checkout)

        if jobserver:
            self.jobserver = jobserver
        else:
            try:
                self.jobserver = zmake.jobserver.GNUMakeJobClient.from_environ()
            except OSError:
                self.jobserver = zmake.jobserver.GNUMakeJobServer(jobs=jobs)

        self.executor = zmake.multiproc.Executor()
        self._sequential = jobs == 1 and not goma

    @property
    def checkout(self):
        if not self._checkout:
            self._checkout = util.locate_cros_checkout()
        return self._checkout.resolve()

    def _resolve_projects(
        self, project_names, all_projects=False, host_tests_only=False
    ) -> List[zmake.project.Project]:
        """Finds all projects for the specified command line flags.

        Returns a list of projects.
        """
        found_projects = zmake.project.find_projects(self.module_paths["ec"] / "zephyr")
        if all_projects:
            projects = found_projects.values()
        elif host_tests_only:
            projects = [p for p in found_projects.values() if p.config.is_test]
        else:
            projects = []
            for project_name in project_names:
                try:
                    projects.append(found_projects[project_name])
                except KeyError as e:
                    raise KeyError("No project named {}".format(project_name)) from e
        return projects

    def configure(
        self,
        project_names,
        build_dir=None,
        toolchain=None,
        build_after_configure=False,
        test_after_configure=False,
        clobber=False,
        bringup=False,
        coverage=False,
        allow_warnings=False,
        all_projects=False,
        host_tests_only=False,
    ):
        """Locate and configure the specified projects."""
        # Resolve build_dir if needed.
        if not build_dir:
            build_dir = self.module_paths["ec"] / "build" / "zephyr"

        projects = self._resolve_projects(
            project_names, all_projects=all_projects, host_tests_only=host_tests_only
        )
        for project in projects:
            project_build_dir = pathlib.Path(build_dir) / project.config.project_name
            self.executor.append(
                func=functools.partial(
                    self._configure,
                    project=project,
                    build_dir=project_build_dir,
                    toolchain=toolchain,
                    build_after_configure=build_after_configure,
                    test_after_configure=test_after_configure,
                    clobber=clobber,
                    bringup=bringup,
                    coverage=coverage,
                    allow_warnings=allow_warnings,
                )
            )
            if self._sequential:
                rv = self.executor.wait()
                if rv:
                    return rv
        rv = self.executor.wait()
        if rv:
            return rv
        test_projects = [p for p in projects if p.config.is_test]
        if len(test_projects) > 1 and coverage and test_after_configure:
            rv = self._merge_lcov_files(
                projects=test_projects,
                build_dir=build_dir,
                output_file=build_dir / "all_tests.info",
            )
            if rv:
                return rv
        non_test_projects = [p for p in projects if not p.config.is_test]
        if len(non_test_projects) > 1 and coverage and build_after_configure:
            rv = self._merge_lcov_files(
                projects=non_test_projects,
                build_dir=build_dir,
                output_file=build_dir / "all_builds.info",
            )
            if rv:
                return rv
        return 0

    def build(
        self,
        project_names,
        build_dir=None,
        toolchain=None,
        clobber=False,
        bringup=False,
        coverage=False,
        allow_warnings=False,
        all_projects=False,
        host_tests_only=False,
    ):
        """Locate and build the specified projects."""
        return self.configure(
            project_names,
            build_dir=build_dir,
            toolchain=toolchain,
            clobber=clobber,
            bringup=bringup,
            coverage=coverage,
            allow_warnings=allow_warnings,
            all_projects=all_projects,
            host_tests_only=host_tests_only,
            build_after_configure=True,
        )

    def test(
        self,
        project_names,
        build_dir=None,
        toolchain=None,
        clobber=False,
        bringup=False,
        coverage=False,
        allow_warnings=False,
        all_projects=False,
        host_tests_only=False,
        no_rebuild=False,
    ):
        """Locate and build the specified projects."""
        if not no_rebuild:
            return self.configure(
                project_names,
                build_dir=build_dir,
                toolchain=toolchain,
                clobber=clobber,
                bringup=bringup,
                coverage=coverage,
                allow_warnings=allow_warnings,
                all_projects=all_projects,
                host_tests_only=host_tests_only,
                test_after_configure=True,
            )
        # Resolve build_dir if needed.
        if not build_dir:
            build_dir = self.module_paths["ec"] / "build" / "zephyr"

        projects = self._resolve_projects(
            project_names, all_projects=all_projects, host_tests_only=host_tests_only
        )
        test_projects = [p for p in projects if p.config.is_test]
        for project in test_projects:
            project_build_dir = pathlib.Path(build_dir) / project.config.project_name
            gcov = "gcov.sh-not-found"
            for build_name, _ in project.iter_builds():
                target_build_dir = project_build_dir / "build-{}".format(build_name)
                gcov = target_build_dir / "gcov.sh"
            self.executor.append(
                func=functools.partial(
                    self._run_test,
                    elf_file=project_build_dir / "output" / "zephyr.elf",
                    coverage=coverage,
                    gcov=gcov,
                    build_dir=project_build_dir,
                    lcov_file=project_build_dir / "output" / "zephyr.info",
                    timeout=project.config.test_timeout_secs,
                )
            )
            if self._sequential:
                rv = self.executor.wait()
                if rv:
                    return rv
        rv = self.executor.wait()
        if rv:
            return rv
        if len(test_projects) > 1 and coverage:
            rv = self._merge_lcov_files(
                projects=test_projects,
                build_dir=build_dir,
                output_file=build_dir / "all_tests.info",
            )
            if rv:
                return rv
        return 0

    def testall(
        self,
        build_dir=None,
        toolchain=None,
        clobber=False,
        bringup=False,
        coverage=False,
        allow_warnings=False,
    ):
        return self.test(
            [],
            build_dir=build_dir,
            toolchain=toolchain,
            clobber=clobber,
            bringup=bringup,
            coverage=coverage,
            allow_warnings=allow_warnings,
            all_projects=True,
        )

    def _configure(
        self,
        project,
        build_dir=None,
        toolchain=None,
        build_after_configure=False,
        test_after_configure=False,
        clobber=False,
        bringup=False,
        coverage=False,
        allow_warnings=False,
    ):
        """Set up a build directory to later be built by "zmake build"."""
        # Resolve build_dir if needed.
        if not build_dir:
            build_dir = (
                self.module_paths["ec"]
                / "build"
                / "zephyr"
                / project.config.project_name
            )

        # Clobber build directory if requested.
        if clobber and build_dir.exists():
            self.logger.info("Clearing build directory %s due to --clobber", build_dir)
            shutil.rmtree(build_dir)

        generated_include_dir = (build_dir / "include").resolve()
        base_config = zmake.build_config.BuildConfig(
            environ_defs={"ZEPHYR_BASE": str(self.zephyr_base), "PATH": "/usr/bin"},
            cmake_defs={
                "CMAKE_EXPORT_COMPILE_COMMANDS": "ON",
                "DTS_ROOT": str(self.module_paths["ec"] / "zephyr"),
                "SYSCALL_INCLUDE_DIRS": str(
                    self.module_paths["ec"] / "zephyr" / "include" / "drivers"
                ),
                "ZMAKE_INCLUDE_DIR": str(generated_include_dir),
            },
        )

        # Prune the module paths to just those required by the project.
        module_paths = project.prune_modules(self.module_paths)

        module_config = zmake.modules.setup_module_symlinks(
            build_dir / "modules", module_paths
        )

        # Symlink the Zephyr base into the build directory so it can
        # be used in the build phase.
        util.update_symlink(self.zephyr_base, build_dir / "zephyr_base")

        dts_overlay_config = project.find_dts_overlays(module_paths)

        toolchain_support = project.get_toolchain(module_paths, override=toolchain)
        toolchain_config = toolchain_support.get_build_config()

        if bringup:
            base_config |= zmake.build_config.BuildConfig(
                kconfig_defs={"CONFIG_PLATFORM_EC_BRINGUP": "y"}
            )
        if coverage:
            base_config |= zmake.build_config.BuildConfig(
                kconfig_defs={"CONFIG_COVERAGE": "y"}
            )
        if allow_warnings:
            base_config |= zmake.build_config.BuildConfig(
                cmake_defs={"ALLOW_WARNINGS": "ON"}
            )
        if self.goma:
            base_config |= zmake.build_config.BuildConfig(
                cmake_defs={
                    "CMAKE_C_COMPILER_LAUNCHER": self.gomacc,
                    "CMAKE_CXX_COMPILER_LAUNCHER": self.gomacc,
                },
            )

        if not build_dir.exists():
            build_dir = build_dir.mkdir()
        if not generated_include_dir.exists():
            generated_include_dir.mkdir()
        processes = []
        files_to_write = []
        self.logger.info("Building %s in %s.", project.config.project_name, build_dir)
        for build_name, build_config in project.iter_builds():
            config = (
                base_config
                | toolchain_config
                | module_config
                | dts_overlay_config
                | build_config
            )

            config_json = config.as_json()
            config_json_file = build_dir / f"cfg-{build_name}.json"
            if config_json_file.is_file():
                if config_json_file.read_text() == config_json:
                    self.logger.info(
                        "Skip reconfiguring %s:%s due to previous cmake run of "
                        "equivalent configuration.  Run with --clobber if this "
                        "optimization is undesired.",
                        project.config.project_name,
                        build_name,
                    )
                    continue
                else:
                    config_json_file.unlink()

            files_to_write.append((config_json_file, config_json))

            output_dir = build_dir / "build-{}".format(build_name)
            if output_dir.exists():
                self.logger.info("Clobber %s due to configuration changes.", output_dir)
                shutil.rmtree(output_dir)

            self.logger.info(
                "Configuring %s:%s.", project.config.project_name, build_name
            )

            kconfig_file = build_dir / "kconfig-{}.conf".format(build_name)
            proc = config.popen_cmake(
                self.jobserver,
                project.config.project_dir,
                output_dir,
                kconfig_file,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                encoding="utf-8",
                errors="replace",
            )
            job_id = "{}:{}".format(project.config.project_name, build_name)
            zmake.multiproc.log_output(
                self.logger,
                logging.DEBUG,
                proc.stdout,
                log_level_override_func=cmake_log_level_override,
                job_id=job_id,
            )
            zmake.multiproc.log_output(
                self.logger,
                logging.ERROR,
                proc.stderr,
                log_level_override_func=cmake_log_level_override,
                job_id=job_id,
            )
            if self._sequential:
                if proc.wait():
                    raise OSError(get_process_failure_msg(proc))
            else:
                processes.append(proc)
        for proc in processes:
            if proc.wait():
                raise OSError(get_process_failure_msg(proc))

        for path, contents in files_to_write:
            path.write_text(contents)

        # To reconstruct a Project object later, we need to know the
        # name and project directory.
        (build_dir / "project_name.txt").write_text(project.config.project_name)
        util.update_symlink(project.config.project_dir, build_dir / "project")

        output_files = []
        if build_after_configure or test_after_configure:
            rv = self._build(
                build_dir=build_dir,
                project=project,
                coverage=coverage,
                output_files_out=output_files,
            )
            if rv:
                return rv
        if test_after_configure and project.config.is_test:
            gcov = "gcov.sh-not-found"
            for build_name, _ in project.iter_builds():
                target_build_dir = build_dir / "build-{}".format(build_name)
                gcov = target_build_dir / "gcov.sh"
            for output_file in output_files:
                self.executor.append(
                    func=functools.partial(
                        self._run_test,
                        elf_file=output_file,
                        coverage=coverage,
                        gcov=gcov,
                        build_dir=build_dir,
                        lcov_file=build_dir / "output" / "zephyr.info",
                        timeout=project.config.test_timeout_secs,
                    )
                )
        return 0

    def _build(
        self,
        build_dir,
        project: zmake.project.Project,
        output_files_out=None,
        coverage=False,
    ):
        """Build a pre-configured build directory."""

        def wait_and_check_success(procs, writers):
            """Wait for processes to complete and check for errors

            Args:
                procs: List of subprocess.Popen objects to check
                writers: List of LogWriter objects to check

            Returns:
                True if all if OK
                False if an error was found (so that zmake should exit)
            """
            bad = None
            for proc in procs:
                if proc.wait() and not bad:
                    bad = proc
            if bad:
                # Just show the first bad process for now. Both builds likely
                # produce the same error anyway. If they don't, the user can
                # still take action on the errors/warnings provided. Showing
                # multiple 'Execution failed' messages is not very friendly
                # since it exposes the fragmented nature of the build.
                raise OSError(get_process_failure_msg(bad))

            # Let all output be produced before exiting
            for writer in writers:
                writer.wait()
            return True

        procs = []
        log_writers = []
        dirs: Dict[str, pathlib.Path] = {}

        build_dir = build_dir.resolve()

        # Compute the version string.
        version_string = zmake.version.get_version_string(
            project,
            build_dir / "zephyr_base",
            zmake.modules.locate_from_directory(build_dir / "modules"),
        )

        # The version header needs to generated during the build phase
        # instead of configure, as the tree may have changed since
        # configure was run.
        zmake.version.write_version_header(
            version_string,
            build_dir / "include" / "ec_version.h",
        )

        gcov = "gcov.sh-not-found"
        for build_name, _ in project.iter_builds():
            with self.jobserver.get_job():
                dirs[build_name] = build_dir / "build-{}".format(build_name)
                gcov = dirs[build_name] / "gcov.sh"
                cmd = ["/usr/bin/ninja", "-C", dirs[build_name].as_posix()]
                if self.goma:
                    # Go nuts ninja, goma does the heavy lifting!
                    cmd.append("-j1024")
                # Only tests will actually build with coverage enabled.
                if coverage and not project.config.is_test:
                    cmd.append("all.libraries")
                self.logger.info(
                    "Building %s:%s: %s",
                    project.config.project_name,
                    build_name,
                    zmake.util.repr_command(cmd),
                )
                proc = self.jobserver.popen(
                    cmd,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    encoding="utf-8",
                    errors="replace",
                )
                job_id = "{}:{}".format(project.config.project_name, build_name)
                dirs[build_name].mkdir(parents=True, exist_ok=True)
                build_log = open(dirs[build_name] / "build.log", "w")
                out = zmake.multiproc.log_output(
                    logger=self.logger,
                    log_level=logging.INFO,
                    file_descriptor=proc.stdout,
                    log_level_override_func=ninja_stdout_log_level_override,
                    job_id=job_id,
                    tee_output=build_log,
                )
                err = zmake.multiproc.log_output(
                    self.logger,
                    logging.ERROR,
                    proc.stderr,
                    job_id=job_id,
                )

                if self._sequential:
                    if not wait_and_check_success([proc], [out, err]):
                        return 2
                else:
                    procs.append(proc)
                    log_writers += [out, err]

        if not wait_and_check_success(procs, log_writers):
            return 2

        # Run the packer.
        packer_work_dir = build_dir / "packer"
        output_dir = build_dir / "output"
        for d in output_dir, packer_work_dir:
            if not d.exists():
                d.mkdir()

        if output_files_out is None:
            output_files_out = []
        # For non-tests, they won't link with coverage, so don't pack the
        # firmware. Also generate a lcov file.
        if coverage and not project.config.is_test:
            with self.jobserver.get_job():
                self._run_lcov(
                    build_dir, output_dir / "zephyr.info", initial=True, gcov=gcov
                )
        else:
            for output_file, output_name in project.packer.pack_firmware(
                packer_work_dir, self.jobserver, version_string=version_string, **dirs
            ):
                shutil.copy2(output_file, output_dir / output_name)
                self.logger.debug("Output file '%s' created.", output_file)
                output_files_out.append(output_file)

        return 0

    def _run_test(
        self, elf_file: pathlib.Path, coverage, gcov, build_dir, lcov_file, timeout=None
    ):
        """Run a single test, with goma if enabled.

        Args:
            elf_file: The path to the ELF to run.
            coverage: True if coverage is enabled.
            gcov: Path to the gcov binary.
            build_dir: Path to the build directory
            lcov_file: Output path for the generated lcov file.
        """
        cmd = []
        if self.goma:
            cmd.append(self.gomacc)
        cmd.append(elf_file)

        def _run():
            self.logger.info("Running tests in %s.", elf_file)
            proc = self.jobserver.popen(
                cmd,
                cwd=elf_file.parent,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                encoding="utf-8",
                errors="replace",
            )
            job_id = "test {}".format(elf_file)
            zmake.multiproc.log_output(
                self.logger,
                logging.DEBUG,
                proc.stdout,
                job_id=job_id,
            )
            zmake.multiproc.log_output(
                self.logger,
                logging.ERROR,
                proc.stderr,
                job_id=job_id,
            )
            try:
                if proc.wait(timeout=timeout):
                    raise OSError(get_process_failure_msg(proc))
                if coverage:
                    self._run_lcov(build_dir, lcov_file, initial=False, gcov=gcov)
            except subprocess.TimeoutExpired as e:
                proc.terminate()
                try:
                    proc.wait(timeout=1)
                except subprocess.TimeoutExpired:
                    proc.kill()
                raise e

        if self.goma:
            _run()
        else:
            with self.jobserver.get_job():
                _run()

    def _run_lcov(self, build_dir, lcov_file, initial=False, gcov=""):
        gcov = os.path.abspath(gcov)
        if initial:
            self.logger.info("Running (initial) lcov on %s.", build_dir)
        else:
            self.logger.info("Running lcov on %s.", build_dir)
        cmd = [
            "/usr/bin/lcov",
            "--gcov-tool",
            gcov,
            "-q",
            "-o",
            "-",
            "-c",
            "-d",
            build_dir,
            "-t",
            build_dir.stem,
            "--rc",
            "lcov_branch_coverage=1",
        ]
        if initial:
            cmd += ["-i"]
        proc = self.jobserver.popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            encoding="utf-8",
            errors="replace",
        )
        zmake.multiproc.log_output(
            self.logger,
            logging.WARNING,
            proc.stderr,
            job_id="{}-lcov".format(build_dir),
        )

        with open(lcov_file, "w") as outfile:
            for line in proc.stdout:
                if line.startswith("SF:"):
                    path = line[3:].rstrip()
                    outfile.write("SF:%s\n" % os.path.realpath(path))
                else:
                    outfile.write(line)
        if proc.wait():
            raise OSError(get_process_failure_msg(proc))

        return 0

    def _merge_lcov_files(self, projects, build_dir, output_file):
        all_lcov_files = []
        for project in projects:
            project_build_dir = pathlib.Path(build_dir) / project.config.project_name
            all_lcov_files.append(project_build_dir / "output" / "zephyr.info")
        with self.jobserver.get_job():
            # Merge info files into a single lcov.info
            self.logger.info("Merging coverage data into %s.", output_file)
            cmd = [
                "/usr/bin/lcov",
                "-o",
                output_file,
                "--rc",
                "lcov_branch_coverage=1",
            ]
            for info in all_lcov_files:
                cmd += ["-a", info]
            proc = self.jobserver.popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                encoding="utf-8",
                errors="replace",
            )
            zmake.multiproc.log_output(
                self.logger, logging.ERROR, proc.stderr, job_id="lcov"
            )
            zmake.multiproc.log_output(
                self.logger, logging.DEBUG, proc.stdout, job_id="lcov"
            )
            if proc.wait():
                raise OSError(get_process_failure_msg(proc))
            return 0

    def list_projects(self, format, search_dir):
        """List project names known to zmake on stdout.

        Args:
            format: The formatting string to print projects with.
            search_dir: Directory to start the search for
                BUILD.py files at.
        """
        if not search_dir:
            search_dir = self.module_paths["ec"] / "zephyr"

        for project in zmake.project.find_projects(search_dir).values():
            print(format.format(config=project.config), end="")

        return 0

    def generate_readme(self, output_file, diff=False):
        """Re-generate the auto-generated README file.

        Args:
            output_file: A pathlib.Path; to be written only if changed.
            diff: Instead of writing out, report the diff.
        """
        expected_contents = zmake.generate_readme.generate_readme()

        if output_file.is_file():
            current_contents = output_file.read_text()
            if expected_contents == current_contents:
                return 0
            if diff:
                self.logger.error(
                    "The auto-generated README.md differs from the expected contents:"
                )
                for line in difflib.unified_diff(
                    current_contents.splitlines(keepends=True),
                    expected_contents.splitlines(keepends=True),
                    str(output_file),
                ):
                    self.logger.error(line.rstrip())
                self.logger.error('Run "zmake generate-readme" to fix this.')
                return 1

        if diff:
            self.logger.error(
                'The README.md file does not exist.  Run "zmake generate-readme".'
            )
            return 1

        output_file.write_text(expected_contents)
        return 0
