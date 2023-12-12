# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# pylint: disable=too-many-lines

"""Module encapsulating Zmake wrapper object."""

import atexit
import difflib
import functools
import logging
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
from typing import Dict, Optional, Set, Union

from zmake import util
import zmake.build_config
import zmake.compare_builds
import zmake.generate_readme
import zmake.jobserver
import zmake.modules
import zmake.multiproc
import zmake.project
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
    # pylint: disable=too-many-return-statements
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
    if line.startswith("devicetree error:"):
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
    return f"Execution failed (return code={proc.returncode}): {util.repr_command(proc.args)}\n"


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

    # pylint: disable=too-many-instance-attributes

    def __init__(
        self,
        checkout=None,
        jobserver: Optional[zmake.jobserver.JobClient] = None,
        jobs=0,
        goma=False,
        gomacc="/mnt/host/depot_tools/.cipd_bin/gomacc",
        modules_dir=None,
        projects_dirs=None,
        zephyr_base=None,
    ):
        zmake.multiproc.LogWriter.reset()
        self.logger = logging.getLogger(self.__class__.__name__)
        self._checkout = checkout
        self.goma = goma
        self.gomacc = gomacc
        if zephyr_base:
            self.zephyr_base = zephyr_base
        else:
            self.zephyr_base = (
                self.checkout / "src" / "third_party" / "zephyr" / "main"
            )
        self.zephyr_base = self.zephyr_base.resolve()

        if modules_dir:
            self.module_paths = zmake.modules.locate_from_directory(modules_dir)
        else:
            self.module_paths = zmake.modules.locate_from_checkout(
                self.checkout
            )

        if projects_dirs:
            self.projects_dirs = []
            for projects_dir in projects_dirs:
                self.projects_dirs.append(projects_dir.resolve())
        else:
            self.projects_dirs = zmake.modules.default_projects_dirs(
                self.module_paths
            )

        if jobserver:
            self.jobserver = jobserver
        else:
            self.jobserver = zmake.jobserver.GNUMakeJobServer(jobs=jobs)

        self.executor = zmake.multiproc.Executor()
        self._sequential = self.jobserver.is_sequential() and not goma
        self.cmp_failed_projects = {}
        self.failed_projects = []

    @property
    def checkout(self):
        """Returns the location of the cros checkout."""
        if not self._checkout:
            self._checkout = util.locate_cros_checkout()
        return self._checkout.resolve()

    def _filter_projects(
        self,
        project_names,
        all_projects=False,
    ):
        """Filter out projects that are not valid for compare builds

        project_names: List of projects passed in to compare-builds
        all_projects: Boolean indicating when "-a" flag used

        Returns a tuple containing:
            set of all projects
            project_names list (filtered)
            all_projects bool
        """
        projects = self._resolve_projects(
            project_names,
            all_projects=all_projects,
        )

        # TODO: b/299112542 - "zmake compare-builds -a" fails to build
        # bloonchipper
        skipped_projects = set(
            filter(
                lambda project: project.config.project_name == "bloonchipper",
                projects,
            )
        )

        for project in skipped_projects:
            self.logger.warning(
                "Project %s not supported by compare-builds, skipping.",
                project.config.project_name,
            )

        projects = projects - skipped_projects

        # Override all_projects setting if any projects are skipped
        if len(skipped_projects) != 0:
            all_projects = False

        project_names = []
        for project in projects:
            project_names.append(project.config.project_name)

        return projects, project_names, all_projects

    def _resolve_projects(
        self,
        project_names,
        all_projects=False,
    ) -> Set[zmake.project.Project]:
        """Finds all projects for the specified command line flags.

        Returns a list of projects.
        """
        found_projects = zmake.project.find_projects(self.projects_dirs)
        if all_projects:
            projects = set(found_projects.values())
        else:
            projects = set()
            for project_name in project_names:
                try:
                    projects.add(found_projects[project_name])
                except KeyError as e:
                    raise KeyError(f"No project named {project_name}") from e
        return projects

    def configure(
        self,
        project_names,
        build_dir=None,
        toolchain=None,
        build_after_configure=False,
        clobber=False,
        bringup=False,
        coverage=False,
        cmake_defs=None,
        cmake_trace=None,
        allow_warnings=False,
        all_projects=False,
        extra_cflags=None,
        delete_intermediates=False,
        version=None,
        static_version=False,
        save_temps=False,
        wait_for_executor=True,
    ):
        """Locate and configure the specified projects."""
        # Resolve build_dir if needed.
        if not build_dir:
            build_dir = self.module_paths["ec"] / "build" / "zephyr"

        projects = self._resolve_projects(
            project_names,
            all_projects=all_projects,
        )
        for project in projects:
            project_build_dir = (
                pathlib.Path(build_dir) / project.config.project_name
            )
            self.executor.append(
                func=functools.partial(
                    self._configure,
                    project=project,
                    build_dir=project_build_dir,
                    toolchain=toolchain,
                    build_after_configure=build_after_configure,
                    clobber=clobber,
                    bringup=bringup,
                    coverage=coverage,
                    cmake_defs=cmake_defs,
                    cmake_trace=cmake_trace,
                    allow_warnings=allow_warnings,
                    extra_cflags=extra_cflags,
                    delete_intermediates=delete_intermediates,
                    version=version,
                    static_version=static_version,
                    save_temps=save_temps,
                )
            )
            if self._sequential:
                result = self.executor.wait()
                if result:
                    return result

        if build_after_configure:
            result = self.executor.wait()
            if result:
                return result
            _db = list(build_dir.glob("*/build-r?/database.bin"))
            if len(_db) > 0:
                univeral_db = build_dir.parent.joinpath("tokens.bin")
                util.merge_token_databases(_db, univeral_db)

        if coverage and build_after_configure:
            result = self.executor.wait()
            if result:
                return result
            result = self._merge_lcov_files(
                projects=projects,
                build_dir=build_dir,
                output_file=build_dir / "all_builds.info",
            )
            if result:
                self.failed_projects.append(str(build_dir / "all_builds.info"))
                return result
        elif wait_for_executor:
            result = self.executor.wait()
            if result:
                return result

        return 0

    def build(
        self,
        project_names,
        build_dir=None,
        toolchain=None,
        clobber=False,
        bringup=False,
        coverage=False,
        cmake_defs=None,
        cmake_trace=None,
        allow_warnings=False,
        all_projects=False,
        extra_cflags=None,
        delete_intermediates=False,
        version=None,
        static_version=False,
        save_temps=False,
    ):
        """Locate and build the specified projects."""
        return self.configure(
            project_names,
            build_dir=build_dir,
            toolchain=toolchain,
            clobber=clobber,
            bringup=bringup,
            coverage=coverage,
            cmake_defs=cmake_defs,
            cmake_trace=cmake_trace,
            allow_warnings=allow_warnings,
            all_projects=all_projects,
            extra_cflags=extra_cflags,
            build_after_configure=True,
            delete_intermediates=delete_intermediates,
            version=version,
            static_version=static_version,
            save_temps=save_temps,
        )

    def compare_builds(
        self,
        ref1,
        ref2,
        project_names,
        toolchain=None,
        all_projects=False,
        extra_cflags=None,
        keep_temps=False,
        cmake_defs=None,
        compare_configs=False,
        compare_binaries_disable=False,
        compare_devicetrees=False,
    ):
        """Compare EC builds at two commits."""
        temp_dir = tempfile.mkdtemp(prefix="zcompare-")
        if not keep_temps:
            atexit.register(shutil.rmtree, temp_dir)
        else:
            self.logger.info("Temporary dir %s will be retained", temp_dir)

        # TODO: b/299112542 - "zmake compare-builds -a" fails to build
        # bloonchipper
        projects, project_names, all_projects = self._filter_projects(
            project_names, all_projects
        )

        if (len(project_names)) == 0 and not all_projects:
            self.logger.info("No projects to compare, exiting.")
            return 0

        self.logger.info("Compare zephyr builds")

        cmp_builds = zmake.compare_builds.CompareBuilds(temp_dir, ref1, ref2)

        for checkout in cmp_builds.checkouts:
            self.logger.info(
                "Checkout %s: full hash %s", checkout.ref, checkout.full_ref
            )

        cmp_builds.do_checkouts(self.zephyr_base, self.module_paths)

        for checkout in cmp_builds.checkouts:
            # Now that the sources have been checked out, transform the
            # zephyr-base and module-paths to use the temporary directory
            # created by BuildInfo.
            for module_name in self.module_paths:
                new_path = checkout.modules_dir / module_name
                transformed_module = {module_name: new_path}
                self.module_paths.update(transformed_module)

            self.projects_dirs = checkout.projects_dirs
            self.zephyr_base = checkout.zephyr_dir

            self.logger.info("Building projects at %s", checkout.ref)
            result = self.configure(
                project_names,
                build_dir=None,
                toolchain=toolchain,
                clobber=False,
                bringup=False,
                coverage=False,
                cmake_defs=cmake_defs,
                allow_warnings=False,
                all_projects=all_projects,
                extra_cflags=extra_cflags,
                build_after_configure=True,
                delete_intermediates=False,
                static_version=True,
                save_temps=False,
                wait_for_executor=False,
            )
            if not result:
                result = self.executor.wait()
            if result:
                self.logger.error(
                    "compare-builds failed to build all projects at %s",
                    checkout.ref,
                )
                return result
        if not compare_binaries_disable:
            failed_projects = cmp_builds.check_binaries(projects)
            self.cmp_failed_projects["binary"] = failed_projects
            self.failed_projects.extend(failed_projects)
        if compare_configs:
            failed_projects = cmp_builds.check_configs(projects)
            self.cmp_failed_projects["config"] = failed_projects
            self.failed_projects.extend(failed_projects)
        if compare_devicetrees:
            failed_projects = cmp_builds.check_devicetrees(projects)
            self.cmp_failed_projects["devicetree"] = failed_projects
            self.failed_projects.extend(failed_projects)

        self.failed_projects = list(set(self.failed_projects))
        if len(self.failed_projects) == 0:
            self.logger.info("Zephyr compare builds successful:")
            for checkout in cmp_builds.checkouts:
                self.logger.info("   %s: %s", checkout.ref, checkout.full_ref)

        return len(self.failed_projects)

    def _configure(
        self,
        project,
        build_dir: pathlib.Path,
        toolchain=None,
        build_after_configure=False,
        clobber=False,
        bringup=False,
        coverage=False,
        cmake_defs=None,
        cmake_trace=None,
        allow_warnings=False,
        extra_cflags=None,
        delete_intermediates=False,
        version=None,
        static_version=False,
        save_temps=False,
    ):
        """Set up a build directory to later be built by "zmake build"."""
        try:
            with self.jobserver.get_job():
                # Clobber build directory if requested.
                if clobber and build_dir.exists():
                    self.logger.info(
                        "Clearing build directory %s due to --clobber",
                        build_dir,
                    )
                    shutil.rmtree(build_dir)

                generated_include_dir = (build_dir / "include").resolve()

                ec_version_flags = []
                if version:
                    ec_version_flags.extend(["--version", version])
                if static_version:
                    ec_version_flags.append("--static")

                base_config = zmake.build_config.BuildConfig(
                    cmake_defs={
                        "CMAKE_EXPORT_COMPILE_COMMANDS": "ON",
                        "DTS_ROOT": str(self.module_paths["ec"] / "zephyr"),
                        "SYSCALL_INCLUDE_DIRS": str(
                            self.module_paths["ec"]
                            / "zephyr"
                            / "include"
                            / "drivers"
                        ),
                        "USER_CACHE_DIR": str(
                            self.module_paths["ec"]
                            / "build"
                            / "zephyr"
                            / "user-cache"
                        ),
                        "ZEPHYR_BASE": str(self.zephyr_base),
                        "ZMAKE_INCLUDE_DIR": str(generated_include_dir),
                        "Python3_EXECUTABLE": sys.executable,
                        **(
                            {
                                "EXTRA_EC_VERSION_FLAGS": util.repr_command(
                                    ec_version_flags
                                )
                            }
                            if ec_version_flags
                            else {}
                        ),
                    },
                )
                if cmake_defs:
                    base_config |= zmake.build_config.BuildConfig.from_args(
                        cmake_defs
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

                toolchain_support = project.get_toolchain(
                    self.module_paths, override=toolchain
                )
                toolchain_config = toolchain_support.get_build_config()

                if bringup:
                    base_config |= zmake.build_config.BuildConfig(
                        kconfig_defs={"CONFIG_PLATFORM_EC_BRINGUP": "y"}
                    )
                if coverage:
                    base_config |= zmake.build_config.BuildConfig(
                        kconfig_defs={"CONFIG_COVERAGE": "y"}
                    )
                if save_temps:
                    base_config |= zmake.build_config.BuildConfig(
                        kconfig_defs={"CONFIG_COMPILER_SAVE_TEMPS": "y"}
                    )
                if not allow_warnings:
                    base_config |= zmake.build_config.BuildConfig(
                        kconfig_defs={"CONFIG_COMPILER_WARNINGS_AS_ERRORS": "y"}
                    )
                if extra_cflags:
                    base_config |= zmake.build_config.BuildConfig(
                        cmake_defs={"EXTRA_CFLAGS": extra_cflags},
                    )
                if self.goma:
                    base_config |= zmake.build_config.BuildConfig(
                        cmake_defs={
                            "CMAKE_C_COMPILER_LAUNCHER": self.gomacc,
                            "CMAKE_CXX_COMPILER_LAUNCHER": self.gomacc,
                        },
                    )

                if not build_dir.exists():
                    build_dir.mkdir()
                if not generated_include_dir.exists():
                    generated_include_dir.mkdir()
                self.logger.info(
                    "Building %s in %s.", project.config.project_name, build_dir
                )
                # To reconstruct a Project object later, we need to know the
                # name and project directory.
                (build_dir / "project_name.txt").write_text(
                    project.config.project_name
                )
                util.update_symlink(
                    project.config.project_dir, build_dir / "project"
                )

                wait_funcs = []
                for build_name, build_config in project.iter_builds():
                    config: zmake.build_config.BuildConfig = (
                        base_config
                        | toolchain_config
                        | module_config
                        | dts_overlay_config
                        | build_config
                    )

                    wait_func = self.executor.append(
                        func=functools.partial(
                            self._configure_one_build,
                            config=config,
                            build_dir=build_dir,
                            build_name=build_name,
                            project=project,
                            cmake_trace=cmake_trace,
                        )
                    )
                    wait_funcs.append(wait_func)
            # Outside the with...get_job above.
            result = 0
            for wait_func in wait_funcs:
                if wait_func():
                    result = 1
            if result:
                self.failed_projects.append(project.config.project_name)
                return 1

            if build_after_configure:
                self._build(
                    build_dir=build_dir,
                    project=project,
                    coverage=coverage,
                    version=version,
                    static_version=static_version,
                    delete_intermediates=delete_intermediates,
                )
            return 0
        except Exception:
            self.failed_projects.append(project.config.project_name)
            raise

    def _configure_one_build(
        self,
        config,
        build_dir,
        build_name,
        project,
        cmake_trace,
    ):
        """Run cmake and maybe ninja on one build dir."""
        with self.jobserver.get_job():
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
                    return 0
                config_json_file.unlink()

            output_dir = build_dir / f"build-{build_name}"
            if output_dir.exists():
                self.logger.info(
                    "Clobber %s due to configuration changes.",
                    output_dir,
                )
                shutil.rmtree(output_dir)

            self.logger.info(
                "Configuring %s:%s.",
                project.config.project_name,
                build_name,
            )

            kconfig_file = build_dir / f"kconfig-{build_name}.conf"
            proc = config.popen_cmake(
                self.jobserver,
                project.config.project_dir,
                output_dir,
                kconfig_file,
                cmake_trace,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                encoding="utf-8",
                errors="replace",
            )
            job_id = f"{project.config.project_name}:{build_name}"
            zmake.multiproc.LogWriter.log_output(
                self.logger,
                logging.DEBUG,
                proc.stdout,
                log_level_override_func=cmake_log_level_override,
                job_id=job_id,
            )
            zmake.multiproc.LogWriter.log_output(
                self.logger,
                logging.ERROR,
                proc.stderr,
                log_level_override_func=cmake_log_level_override,
                job_id=job_id,
            )
            if proc.wait():
                raise OSError(get_process_failure_msg(proc))
            config_json_file.write_text(config_json)
            return 0

    def _build(
        self,
        build_dir,
        project: zmake.project.Project,
        coverage=False,
        version=None,
        static_version=False,
        delete_intermediates=False,
    ):
        """Build a pre-configured build directory."""

        with self.jobserver.get_job():
            dirs: Dict[str, pathlib.Path] = {}

            build_dir = build_dir.resolve()

            # Compute the version string.
            version_string = zmake.version.get_version_string(
                project.config.project_name,
                version,
                static=static_version,
            )

            # The version header needs to generated during the build phase
            # instead of configure, as the tree may have changed since
            # configure was run.
            zmake.version.write_version_header(
                version_string,
                build_dir / "include" / "ec_version.h",
                "zmake",
                static=static_version,
            )

            gcov = "gcov.sh-not-found"
            wait_funcs = []
            for build_name, _ in project.iter_builds():
                dirs[build_name] = build_dir / f"build-{build_name}"
                gcov = dirs[build_name] / "gcov.sh"
                wait_func = self.executor.append(
                    func=functools.partial(
                        self._build_one_dir,
                        build_name=build_name,
                        dirs=dirs,
                        coverage=coverage,
                        project=project,
                    )
                )
                wait_funcs.append(wait_func)
        # Outside the with...get_job above.
        result = 0
        for wait_func in wait_funcs:
            if wait_func():
                result = 1
        if result:
            self.failed_projects.append(project.config.project_name)
            return 1

        with self.jobserver.get_job():
            # Run the packer.
            packer_work_dir = build_dir / "packer"
            output_dir = build_dir / "output"
            for newdir in output_dir, packer_work_dir:
                if not newdir.exists():
                    newdir.mkdir()

            # Projects won't link with coverage, so don't pack the firmware.
            # Also generate a lcov file.
            if coverage:
                self._run_lcov(
                    build_dir,
                    output_dir / "zephyr.info",
                    initial=True,
                    gcov=gcov,
                )
            else:
                unsigned_files = project.packer.pack_firmware(
                    packer_work_dir,
                    self.jobserver,
                    dirs,
                    version_string=version_string,
                )
                for output_file, output_name in project.signer.sign(
                    unsigned_files, packer_work_dir, self.jobserver
                ):
                    shutil.copy2(output_file, output_dir / output_name)
                    self.logger.debug(
                        "Output file '%s' created.", output_dir / output_name
                    )

            if delete_intermediates:
                outdir = build_dir / "output"
                for child in build_dir.iterdir():
                    if child != outdir:
                        logging.debug("Deleting %s", child)
                        if not child.is_symlink() and child.is_dir():
                            shutil.rmtree(child)
                        else:
                            child.unlink()
            return 0

    def _build_one_dir(self, build_name, dirs, coverage, project):
        """Builds one sub-dir of a configured project (build-ro, etc)."""

        with self.jobserver.get_job():
            cmd = [
                util.get_tool_path("ninja"),
                "-C",
                dirs[build_name].as_posix(),
            ]
            if self.goma:
                # Go nuts ninja, goma does the heavy lifting!
                cmd.append("-j1024")
            elif self._sequential:
                cmd.append("-j1")
            if coverage:
                cmd.append("all.libraries")
            self.logger.info(
                "Building %s:%s: %s",
                project.config.project_name,
                build_name,
                util.repr_command(cmd),
            )
            proc = self.jobserver.popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                encoding="utf-8",
                errors="replace",
                # TODO(b/239619222): Filter os.environ for ninja.
                env=os.environ,
            )
            job_id = f"{project.config.project_name}:{build_name}"
            dirs[build_name].mkdir(parents=True, exist_ok=True)
            build_log = open(  # pylint:disable=consider-using-with
                dirs[build_name] / "build.log",
                "w",
                encoding="utf-8",
            )
            out = zmake.multiproc.LogWriter.log_output(
                logger=self.logger,
                log_level=logging.INFO,
                file_descriptor=proc.stdout,
                log_level_override_func=ninja_stdout_log_level_override,
                job_id=job_id,
                tee_output=build_log,
            )
            err = zmake.multiproc.LogWriter.log_output(
                self.logger,
                logging.ERROR,
                proc.stderr,
                job_id=job_id,
            )

            if proc.wait():
                raise OSError(get_process_failure_msg(proc))

            # Let all output be produced before exiting
            out.wait()
            err.wait()
            return 0

    def _run_lcov(
        self,
        build_dir,
        lcov_file,
        initial=False,
        gcov: Union[os.PathLike, str] = "",
    ):
        gcov = os.path.abspath(gcov)
        if initial:
            self.logger.info("Running (initial) lcov on %s.", build_dir)
        else:
            self.logger.info("Running lcov on %s.", build_dir)
        cmd = [
            util.get_tool_path("lcov"),
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
        zmake.multiproc.LogWriter.log_output(
            self.logger,
            logging.WARNING,
            proc.stderr,
            job_id=f"{build_dir}-lcov",
        )

        with open(lcov_file, "w", encoding="utf-8") as outfile:
            for line in proc.stdout:
                if line.startswith("SF:"):
                    path = line[3:].rstrip()
                    outfile.write(f"SF:{os.path.realpath(path)}\n")
                else:
                    outfile.write(line)
        if proc.wait():
            raise OSError(get_process_failure_msg(proc))

        return 0

    def _merge_lcov_files(self, projects, build_dir, output_file):
        all_lcov_files = []
        for project in projects:
            project_build_dir = (
                pathlib.Path(build_dir) / project.config.project_name
            )
            all_lcov_files.append(project_build_dir / "output" / "zephyr.info")
        # Merge info files into a single lcov.info
        self.logger.info("Merging coverage data into %s.", output_file)
        cmd = [
            util.get_tool_path("lcov"),
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
        zmake.multiproc.LogWriter.log_output(
            self.logger, logging.ERROR, proc.stderr, job_id="lcov"
        )
        zmake.multiproc.LogWriter.log_output(
            self.logger, logging.DEBUG, proc.stdout, job_id="lcov"
        )
        if proc.wait():
            raise OSError(get_process_failure_msg(proc))
        return 0

    def list_projects(self, fmt):
        """List project names known to zmake on stdout.

        Args:
            fmt: The formatting string to print projects with.
        """
        for project in zmake.project.find_projects(self.projects_dirs).values():
            print(fmt.format(config=project.config), end="")

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
