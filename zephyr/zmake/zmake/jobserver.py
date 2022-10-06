# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Module for job counters, limiting the amount of concurrent executions."""

import logging
import multiprocessing
import os
import re
import select
import shlex
import subprocess

import zmake


class JobHandle:
    """Small object to handle claim of a job."""

    def __init__(self, release_func, *args, **kwargs):
        self.release_func = release_func
        self.args = args
        self.kwargs = kwargs

    def __enter__(self):
        pass

    def __exit__(self, exc_type, exc_value, traceback):
        self.release_func(*self.args, **self.kwargs)


class JobClient:
    """Abstract base class for all job clients."""

    def get_job(self):
        """Claim a job."""
        raise NotImplementedError("Abstract method not implemented")

    @staticmethod
    def env():
        """Get the environment variables necessary to share the job server."""
        return {}

    def popen(self, argv, **kwargs):
        """Start a process using subprocess.Popen

        All other arguments are passed to subprocess.Popen.

        Returns:
            A Popen object.
        """
        # By default, we scrub the environment for all commands we run, setting
        # the bare minimum (PATH only).  This prevents us from building obscure
        # dependencies on the environment variables.
        kwargs.setdefault("env", {"PATH": "/bin:/usr/bin"})
        kwargs["env"].update(self.env())

        logger = logging.getLogger(self.__class__.__name__)
        logger.debug(
            "Running `env -i %s%s%s`",
            " ".join(f"{k}={shlex.quote(v)}" for k, v in kwargs["env"].items()),
            " " if kwargs["env"] else "",
            zmake.util.repr_command(argv),
        )
        return subprocess.Popen(argv, **kwargs)


class GNUMakeJobClient(JobClient):
    """A job client for GNU make."""

    def __init__(self, read_fd, write_fd):
        self._pipe = [read_fd, write_fd]

    @classmethod
    def from_environ(cls, env=None):
        """Create a job client from an environment with the MAKEFLAGS variable.

        If we are started under a GNU Make Job Server, we can search
        the environment for a string "--jobserver-auth=R,W", where R
        and W will be the read and write file descriptors to the pipe
        respectively.  If we don't find this environment variable (or
        the string inside of it), this will raise an OSError.

        Args:
            env: Optionally, the environment to search.

        Returns:
            A GNUMakeJobClient configured appropriately.
        """
        if env is None:
            env = os.environ
        makeflags = env.get("MAKEFLAGS")
        if not makeflags:
            raise OSError("MAKEFLAGS is not set in the environment")
        match = re.search(r"--jobserver-auth=(\d+),(\d+)", makeflags)
        if not match:
            raise OSError("MAKEFLAGS did not contain jobserver flags")
        read_fd, write_fd = map(int, match.groups())
        return cls(read_fd, write_fd)

    def get_job(self):
        """Claim a job.

        Returns:
            A JobHandle object.
        """
        byte = os.read(self._pipe[0], 1)
        return JobHandle(lambda: os.write(self._pipe[1], byte))

    def env(self):
        """Get the environment variables necessary to share the job server."""
        return {"MAKEFLAGS": "--jobserver-auth={},{}".format(*self._pipe)}


class GNUMakeJobServer(GNUMakeJobClient):
    """Implements a GNU Make POSIX Job Server.

    See https://www.gnu.org/software/make/manual/html_node/POSIX-Jobserver.html
    for specification.
    """

    def __init__(self, jobs=0):
        [read_fd, write_fd] = os.pipe()
        super().__init__(read_fd, write_fd)
        if not jobs:
            jobs = multiprocessing.cpu_count()
        elif jobs > select.PIPE_BUF:
            jobs = select.PIPE_BUF

        os.write(self._pipe[1], b"+" * jobs)
