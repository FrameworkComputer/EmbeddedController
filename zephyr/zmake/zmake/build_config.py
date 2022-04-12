# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Encapsulation of a build configuration."""

import hashlib
import json
import pathlib

import zmake.jobserver
import zmake.util as util


class BuildConfig:
    """A container for build configurations.

    A build config is a tuple of environment variables, cmake
    variables, kconfig definitons, and kconfig files.
    """

    def __init__(
        self, environ_defs=None, cmake_defs=None, kconfig_defs=None, kconfig_files=None
    ):
        self.environ_defs = dict(environ_defs or {})
        self.cmake_defs = dict(cmake_defs or {})
        self.kconfig_defs = dict(kconfig_defs or {})

        def _remove_duplicate_paths(files):
            # Remove multiple of the same kconfig file in a row.
            result = []
            for path in files:
                if not result or path != result[-1]:
                    result.append(path)
            return result

        self.kconfig_files = _remove_duplicate_paths(kconfig_files or [])

    def popen_cmake(
        self,
        jobclient: zmake.jobserver.JobClient,
        project_dir,
        build_dir,
        kconfig_path=None,
        **kwargs
    ):
        """Run Cmake with this config using a jobclient.

        Args:
            jobclient: A JobClient instance.
            project_dir: The project directory.
            build_dir: Directory to use for Cmake build.
            kconfig_path: The path to write out Kconfig definitions.
            kwargs: forwarded to popen.
        """
        kconfig_files = list(self.kconfig_files)
        if kconfig_path:
            util.write_kconfig_file(kconfig_path, self.kconfig_defs)
            kconfig_files.append(kconfig_path)
        elif self.kconfig_defs:
            raise ValueError(
                "Cannot start Cmake on a config with Kconfig items without a "
                "kconfig_path"
            )

        if kconfig_files:
            base_config = BuildConfig(
                environ_defs=self.environ_defs, cmake_defs=self.cmake_defs
            )
            conf_file_config = BuildConfig(
                cmake_defs={
                    "CONF_FILE": ";".join(str(p.resolve()) for p in kconfig_files)
                }
            )
            return (base_config | conf_file_config).popen_cmake(
                jobclient, project_dir, build_dir, **kwargs
            )

        kwargs["env"] = dict(**kwargs.get("env", {}), **self.environ_defs)
        return jobclient.popen(
            [
                "/usr/bin/cmake",
                "-S",
                project_dir,
                "-B",
                build_dir,
                "-GNinja",
                *("-D{}={}".format(*pair) for pair in self.cmake_defs.items()),
            ],
            **kwargs
        )

    def __or__(self, other):
        """Combine two BuildConfig instances."""
        if not isinstance(other, BuildConfig):
            raise TypeError(
                "Unsupported operation | for {} and {}".format(type(self), type(other))
            )

        return BuildConfig(
            environ_defs=dict(**self.environ_defs, **other.environ_defs),
            cmake_defs=dict(**self.cmake_defs, **other.cmake_defs),
            kconfig_defs=dict(**self.kconfig_defs, **other.kconfig_defs),
            kconfig_files=[*self.kconfig_files, *other.kconfig_files],
        )

    def __repr__(self):
        return "BuildConfig({})".format(
            ", ".join(
                "{}={!r}".format(name, getattr(self, name))
                for name in [
                    "environ_defs",
                    "cmake_defs",
                    "kconfig_defs",
                    "kconfig_files",
                ]
                if getattr(self, name)
            )
        )

    def _get_paths_for_hashing(self):
        # Zephyr's CMake system won't detect that CMake needs to be
        # re-run to regenerate Kconfig headers or merge DTS files.
        # We can work around this in Zmake by hashing the file
        # contents of known problematic paths, and forcing a clobber
        # when any hash is changed.
        #
        # TODO(b/215560602): Delete this code when Zephyr's Cmake
        # system is fixed.
        paths_for_hashing = set()

        for path in self.cmake_defs.get("DTC_OVERLAY_FILE", "").split(";"):
            if not path:
                continue
            paths_for_hashing.add(pathlib.Path(path).resolve())

        for path in self.kconfig_files:
            paths_for_hashing.add(path.resolve())

        return paths_for_hashing

    def _get_file_hashes(self):
        result = {}

        for path in self._get_paths_for_hashing():
            result[str(path)] = hashlib.sha224(path.read_bytes()).hexdigest()

        return result

    def as_json(self):
        """Provide a stable JSON representation of the build config."""
        return json.dumps(
            {
                "environ_defs": self.environ_defs,
                "cmake_defs": self.cmake_defs,
                "kconfig_defs": self.kconfig_defs,
                "kconfig_files": [str(p.resolve()) for p in self.kconfig_files],
                "file_hashes": self._get_file_hashes(),
            },
            sort_keys=True,
        )
